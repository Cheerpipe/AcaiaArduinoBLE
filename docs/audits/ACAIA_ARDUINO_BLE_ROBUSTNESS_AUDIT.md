# AcaiaArduinoBLE Robustness Audit

Date: 2026-08-09  
Reviewed tree: current working state based on `45e84d7` (includes uncommitted local changes)  
Library: AcaiaArduinoBLE 3.3.0  
Dependency used for verification: ArduinoBLE 2.1.0  

## Verdict

The library **should not yet be considered suitable for prolonged use inside a
safety control**. The main blocker is the incorrect `BLECharacteristic`
lifecycle: the code uses an implicit assignment operator that ArduinoBLE does
not implement safely. This can leave dangling pointers after a disconnection
and result in a crash or memory corruption during reconnection.

There is also an out-of-bounds read when debug is enabled, uninitialized timing
state, connections that are not cleaned up on several error paths, no timeout
before the first packet, and insufficient frame validation. In addition, the
ESP32 implementation in ArduinoBLE 2.1.0 contains unlimited waits. That last
risk resides in the dependency rather than AcaiaArduinoBLE, but it directly
affects the library's ability to guarantee progress.

A general rewrite is not recommended. Remediation should first focus on A-01
through A-07 and include reconnection and corrupted-frame tests.

## Severity scale

- **Critical**: can cause memory corruption/a crash or invalidate an essential
  progress guarantee.
- **High**: can preserve a false link, accept dangerously incorrect data, or
  prevent recovery from a normal failure.
- **Medium**: a real API or availability defect with bounded or conditional
  impact.
- **Low/improvement**: recommended hardening, but it should be applied only
  when justified by a measurement or requirement.

## Findings summary

| ID | Severity | Type | Finding | Status |
|---|---:|---|---|---|
| A-01 | Critical | Defect | Unsafe `BLECharacteristic` assignment; possible use-after-free | Confirmed by code and compiler |
| A-02 | High | Defect | Out-of-buffer read when printing long packets in debug mode | Confirmed |
| A-03 | High | Defect | Error paths leave connection and handles partially initialized | Confirmed |
| A-04 | High | Defect | A connection without a first packet never expires | Confirmed |
| A-05 | High | Defect/hardening | BLE frames are accepted without sufficient integrity or plausibility checks | Confirmed |
| A-06 | High | Defect | `_lastHeartBeat` is read without initialization | Confirmed |
| A-07 | High | Dependency | ArduinoBLE/ESP32 contains unlimited blocking and spin loops | Confirmed in installed 2.1.0 |
| A-08 | Medium | Defect | Only a 100 ms discovery window and mishandled scan errors | Confirmed |
| A-09 | Medium | API defect | Public commands allow incompatible protocols or unexpected state changes | Confirmed |
| A-10 | Medium | Missing contract | The class is not thread-safe and declares no single owner | Confirmed |
| A-11 | Low | Improvement | Insufficient version control, tests, dynamic-allocation checks, and link telemetry | Conditional recommendation |

## Detailed findings

### A-01 — Critical — `BLECharacteristic` has no valid ownership

**Evidence.** All eight `_write` and `_read` assignments use expressions such
as `_write = peripheral.characteristic(...)` in
[AcaiaArduinoBLE.cpp](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L114).
ArduinoBLE 2.1.0 declares a copy constructor and destructor for
`BLECharacteristic`, but no assignment operator. Its copy constructor calls
`retain()` and its destructor calls `release()`; the implicit operator only
copies pointers.

ESP32, C3, and S3 builds with `--warnings all` emitted this warning eight times:

```text
warning: implicitly-declared 'BLECharacteristic::operator=(const BLECharacteristic&)'
is deprecated [-Wdeprecated-copy]
note: because 'BLECharacteristic' has user-provided
'BLECharacteristic::BLECharacteristic(const BLECharacteristic&)'
```

**Possible failure.** The temporary returned by `characteristic()` retains the
remote object, but releases that reference when destroyed. The member receives
the pointer without acquiring its own reference. When ArduinoBLE destroys peer
attributes on disconnect, `_read`/`_write` may point to freed memory. A later
call to `valueUpdated()`, `writeValue()`, or the destructor can become a
use-after-free.

**Minimum recommended fix.** Do not hide the warning. Choose and explicitly
test one of these solutions:

1. fix and pin an ArduinoBLE fork that correctly implements the rule of five
   for `BLECharacteristic`; or
2. change AcaiaArduinoBLE to explicitly reconstruct handles with valid
   ownership and destroy them before disconnection, without using the defective
   assignment operator.

There must be one `resetConnection()` function that destroys or invalidates
both handles before releasing the peer. Compile with `-Werror=deprecated-copy`
to prevent regression.

**Acceptance criterion.** Thousands of connect/disconnect/reconnect cycles,
remote disconnection at every phase, and concurrently requested commands,
without ASan/use-after-free errors on a simulated backend or copy warnings.

### A-02 — High — out-of-bounds debug read

**Evidence.** [AcaiaArduinoBLE.cpp](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L339)
allocates `input[13]` and reads no more than 13 bytes. For 14, 17, 18, or
20-byte packets it calls `printData(input, l)` at line 356. `printData()` walks
exactly `length` bytes in
[AcaiaArduinoBLE.cpp](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L487).

**Possible failure.** With `_debug == true`, between one and seven bytes are
read beyond the stack buffer. This can expose memory through Serial, alter
decisions through undefined behavior, or cause a crash depending on platform
and optimization. The current stopper constructs the class with `DEBUG=false`,
so that specific configuration does not activate the fault; the public library
still exposes it.

**Minimum recommended fix.** Print only the number of bytes actually read, not
`l`. Check the `readValue()` result and use it for both parsing and debug.
Alternatively, size the buffer to the real protocol maximum, without reading
more bytes again than required.

### A-03 — High — partial state and connection not released on errors

**Evidence.** After `peripheral.connect()`, failures for unknown type,
subscription, `IDENTIFY`, notification request, and Felicita mode return without
`peripheral.disconnect()` in
[AcaiaArduinoBLE.cpp](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L113).
Only discovery failure explicitly disconnects. `init()` also does not force
`_connected=false` at entry. Commands set `_connected=false` when a write
fails, but do not disconnect or invalidate handles, for example at
[AcaiaArduinoBLE.cpp](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L188).

**Possible failure.** A peer remains connected while the class reports itself
disconnected; the next scan/reconnect may exhaust slots or reuse dangling
handles. Cached state can survive a failed attempt and diverge from actual ATT
state.

**Minimum recommended fix.** Implement one idempotent error/cleanup path that:

- marks the connection disconnected;
- invalidates handles with correct ownership;
- cancels scanning when active;
- disconnects the peer/ATT when appropriate;
- resets timestamps and type only after cleanup.

Do not duplicate manual cleanup across branches; that increases the chance that
a new path will omit it.

### A-04 — High — no timeout for the first packet

**Evidence.** `init()` sets `_lastPacket = 0`.
[newWeightAvailable()](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L328)
applies the timeout only when `_lastPacket` is nonzero.

**Possible failure.** If subscribe/identify responds but no weight ever arrives,
`_connected` remains true indefinitely. In the stopper this can present the
scale as available, prevent reconnection, and begin an automatic cycle without
a single valid sample.

**Minimum recommended fix.** Store `connectedAtMs` and require the first valid
packet within an explicit deadline. Then use `lastValidPacketMs`, not mere
arrival of any notification. On expiry, fully clean the connection through the
common A-03 path.

### A-05 — High — parser lacks sufficient integrity and plausibility checks

**Evidence.** [newWeightAvailable()](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L328)
decides primarily by length and a few bytes. It does not uniformly validate
header, opcode, checksum, ASCII digits, unit/exponent, physical range, or rate
of change. Felicita characters are subtracted from `'0'` without checking that
they are digits; the Acaia exponent is passed directly to `pow()`; the
`readValue()` result is ignored.

**Possible failure.** Interference, desynchronization, or an incompatible
peripheral can produce a finite but false weight. A consumer such as the
stopper may add it to regression and open CN9 earlier or later than expected.
Correct length does not prove integrity.

**Minimum recommended fix.** Create per-protocol validators and reject data
before modifying `_currentWeight`/`_lastPacket`:

- exact frame size and exact byte count read;
- expected header/opcode/state;
- checksum where defined by the protocol;
- permitted digits, sign, unit, and scale;
- a finite result within the scale's physical range;
- a configurable consumer-side slew/outlier filter.

Do not invent checksums: use captured vectors to document which fields exist
for each model. Unknown packets must degrade availability, not become zero or a
plausible weight.

### A-06 — High — uninitialized heartbeat timestamp

**Evidence.** The constructor initializes weight, connection, type, and period,
but not `_lastHeartBeat` or `_lastPacket` in
[AcaiaArduinoBLE.cpp](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L38).
`init()` resets only `_lastPacket`.
[heartbeatRequired()](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L318)
reads `_lastHeartBeat` before `heartbeat()` has necessarily been called.

**Possible failure.** This reads an indeterminate C++ value. It may delay the
first heartbeat, send it immediately and nondeterministically, or cause
undefined behavior under optimization.

**Minimum recommended fix.** Initialize every member in an initializer list.
Use `uint32_t` for timestamps and modular subtraction. Explicitly define at
connection time whether the first heartbeat is immediate or occurs after
`HEARTBEAT_PERIOD_MS`.

### A-07 — High — the ESP32 ArduinoBLE dependency does not guarantee progress

**Evidence.** In the installed ArduinoBLE 2.1.0,
`src/utility/HCIVirtualTransport.cpp` contains:

- `xStreamBufferSend(..., portMAX_DELAY)` in receive and write paths;
- `xStreamBufferReceive(..., portMAX_DELAY)`;
- `while (!esp_vhci_host_check_send_available()) {}` without yield or timeout;
- blocking `read()` with `portMAX_DELAY`;
- `begin()` ignores results from buffer creation, controller
  initialization/enable, and task creation, then reports success.

Espressif's official documentation identifies high-priority task spin loops as
a starvation cause and recommends explicit TWDT supervision:
[Task Watchdog](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/system/wdts.html).

**Possible failure.** A stuck HCI controller can block the BLE owner forever or
consume a core. AcaiaArduinoBLE cannot impose a higher-level timeout when the
underlying call never returns.

**Minimum recommended fix.** Pin and audit a specific ArduinoBLE version or
maintain a hardened ESP32 fork with:

- finite timeouts and error codes;
- yield or timed blocking instead of spinning;
- validation and rollback for every initialization step;
- an observable health counter and controlled reinitialization capability.

Changing the BLE stack should be considered only when those guarantees cannot
be added and after compatibility testing with every scale model; it is not an
automatic recommendation for a rewrite.

### A-08 — Medium — discovery is too short and scan errors are misdiagnosed

**Evidence.** [init()](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L46)
searches for only 100 ms. The `BLE.scan()` result is ignored.
`scanForAddress()` reports whether scanning started, but its message interprets
the result as “Failed to find.”

**Possible failure.** Peripherals with slow advertising are found
intermittently. In a starved system, almost the entire window can pass without
this task executing.

**Minimum recommended fix.** Split `startScan`, `poll`, and the deadline into a
nonblocking state machine; use a deadline spanning several advertising
intervals with backoff and jitter. Log “scan did not start,” “deadline without
advertisement,” “connect failed,” and “discovery failed” separately.

### A-09 — Medium — public commands do not enforce capabilities

**Evidence.** `tareStartTimer()` always sends the Bookoo command even though
`supportsTareStartTimer()` exists. `beep()` uses the Bookoo beep for `GENERIC`
and the six-byte Acaia tare command for every other type, including Felicita,
in [AcaiaArduinoBLE.cpp](../../libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.cpp#L256).

**Possible failure.** A caller that omits the precheck can send an incompatible
command. `beep()` may tare an Acaia and sends the wrong protocol to Felicita. It
is not a side-effect-free beep.

**Minimum recommended fix.** Make each method validate connection and
capability internally. Rename the side-effecting operation
(`tareForAudibleFeedback`) or remove `beep()` in favor of
`beepWithoutStateChange()`. Never rely only on callers invoking `supports...()`.

### A-10 — Medium — missing concurrency contract

**Evidence.** State, handles, and timestamps are mutable and unsynchronized. No
comment or API requires a single owning task.

**Possible failure.** Two tasks calling `newWeightAvailable()`, `heartbeat()`,
and commands can interleave access to the same ATT/handle and cached state. The
current stopper does centralize those operations in `scale_worker`, so this
race was not observed in that consumer.

**Minimum recommended fix.** Prefer an explicit single-owner-task contract and
enforce it in tests/debug assertions. Add a mutex only when real multithreaded
consumers exist; do not add one preemptively around calls that may block
indefinitely.

### A-11 — Low — maintainability and observability improvements

Apply only when justified by profiling or requirements:

- `libraries/AcaiaArduinoBLE/library.properties` does not pin an ArduinoBLE
  version; document and validate an exact matrix.
- AcaiaArduinoBLE has no tests. Add a fake backend for lifecycle, timeout, and
  frame-corpus testing.
- `String` in scanning/names may fragment the heap during prolonged
  reconnection; first measure `largest_free_block` and minimum heap, then use
  bounded comparison only if evidence supports it.
- Global command arrays are mutable; make them `static const`/`constexpr` where
  the platform permits.
- Expose disconnection reason, age of last valid packet, rejected-packet count,
  and reconnection count.

## Existing controls worth retaining

- Felicita commands now use their actual one-byte size, preventing reads beyond
  the array.
- The parser limits notification copies to 13 bytes, enough for the currently
  used indexes. A-02 concerns debug output, not this bounded copy.
- Common commands report success/failure so consumers can degrade to manual
  mode.
- The stopper calls the library from a single BLE task, which is a sound
  isolation decision.
- Consumer timing arithmetic mostly uses `uint32_t`; the library should align
  with that pattern.

## Verification performed

### Real compilation

Arduino CLI, `esp32:esp32` core 3.3.3, ArduinoBLE 2.1.0, `--warnings all`:

| Target | Result | Flash | Global RAM | Project-specific observation |
|---|---:|---:|---:|---|
| ESP32 Dev Module | OK | 1,214,375 B / 92% | 62,008 B / 18% | 8 A-01 warnings |
| ESP32-C3 Dev Module | OK | 1,266,280 B / 96% | 50,024 B / 15% | 8 A-01 warnings |
| ESP32-S3 Dev Module | OK | 1,154,991 B / 88% | 58,476 B / 17% | 8 A-01 warnings |

Additional C3/S3 warnings come from core/IDF headers rather than this library.
Successful compilation proves syntactic compatibility, not lifecycle safety.

### Audit limitations

- The repository contains neither library unit tests nor a fake ArduinoBLE
  backend.
- No frame fuzzing or prolonged physical reconnection testing was performed
  with every model.
- Stopper ASan/UBSan does not instrument ArduinoBLE or MCU firmware.
- RF behavior, power consumption, brownout, electromagnetic interference, and
  real advertising were not measured.

## Recommended remediation order

1. A-01 and the common A-03 cleanup path.
2. A-02 and A-06, because they are small undefined-behavior fixes.
3. A-04 and A-05, with first-packet timeout and per-protocol corpus tests.
4. A-07: a validated ArduinoBLE pin/fork with timeouts and health monitoring.
5. A-08 and A-09.
6. A-10/A-11 only where supported by a requirement or measurement.

## Minimum tests before release

- 10,000 connection/disconnection cycles for every supported family.
- Disconnection during scan, connect, discovery, subscribe, identify,
  heartbeat, read, and every write.
- A peripheral that connects but never notifies; it must expire and reconnect.
- A corpus covering lengths 0–64, truncated frames, invalid checksums, extreme
  exponents, invalid ASCII, derived `NaN`/infinity values, and physically
  impossible jumps.
- Queue/heap under pressure and forced BLE-stack allocation failures.
- At least a 72-hour soak with RF loss, scale power cycling, and logging of
  minimum heap, largest block, stack high-water mark, and every reconnection
  reason.

Until A-01 through A-07 and these tests are complete, consumers must treat any
BLE loss or ambiguity as **scale unavailable**, never as permission for a more
dangerous action.

# Shot Stopper Robustness and Safety Audit

Date: 2026-08-09  
Reviewed tree: current working state based on `45e84d7` (includes uncommitted local changes)  
Verified targets: ESP32, ESP32-C3, and ESP32-S3 with Espressif core 3.3.3  

## Remediation status — 2026-08-09

The prioritized software measures in this report were implemented in the
post-audit working tree. Remediation includes transactional CN9 closing, an
IRAM GPTimer, per-task Task Watchdog supervision, maintenance reservations,
Web session ownership, weight freshness and plausibility validation,
critical queues without infinite waits, bounded retries, persistence with
read-back, unique credentials, network recovery, and health telemetry.

The only blocker that code alone cannot resolve is the physical independence
required by SS-01. The firmware provides heartbeat and feedback interfaces for
that integration, but the K2 barrier, its external detector, isolation, and HIL
tests must exist in the actual hardware. Control remediation is verified by
host tests and builds. Remaining risks are the accepted use of HTTP without TLS
while remote actuation is disabled, and the conditional schema/wear
improvements in SS-13. Release for a real machine remains conditional on the
electrical and HIL tests specified in this report.

## Original verdict — before remediation

The stopper contains valuable defenses: a normally-open relay, a safe state
established at the start of `setup()`, a global limit, an explicit state
machine, BLE isolation, configuration validation, redundant storage, and a
reasonably broad host suite. No firmware-owned lock acquisition cycle forming
a deadlock was found.

Nevertheless, **it was not yet releasable as the only safety element of a
machine containing a boiler and steam**. There were two critical blockers:

1. Both CN9 limits were software timers dispatched by the same system; they
   were not an independent barrier against a SoC lockup, power failure, GPIO
   fault, shorted transistor, or welded relay.
2. A race existed between arming the timers and publishing/closing CN9. If the
   control task was suspended during that interval, callbacks could consume
   their one-shots while observing `cn9Closed == false`; the resumed task could
   then close CN9 without an active timer.

In addition, elevated `loopTask` priority on ESP32-C3 could almost completely
starve the BLE worker and network manager, explaining connectivity/UI failures
while the loop continued feeding its watchdog. Other configuration/persistence
races existed, and automatic decisions did not require a fresh weight sample.

A full rewrite was not recommended. The recommendation was to fix SS-01 through
SS-07 first, add an independent physical barrier, and verify it with fault
injection. The remaining work should be prioritized by soak measurements and
the actual threat model.

## Severity scale

- **Critical**: can leave CN9 closed without a valid opening guarantee or let a
  single controller fault invalidate all safety protection.
- **High**: can cause prolonged starvation, automatic actuation using untrusted
  data, or a dangerous concurrent mutation.
- **Medium**: real loss of command/state, incomplete recovery, or bounded
  blocking.
- **Low/improvement**: hardening and observability; measure before refactoring.

## Findings summary

| ID | Severity | Type | Finding | Status |
|---|---:|---|---|---|
| SS-01 | Critical | Safety architecture | No ESP32-independent cutoff or real relay/CN9 feedback | Interface implemented; physical K2 and HIL pending |
| SS-02 | Critical | Race | Timers armed before the `cn9Closed` commit; a callback could be lost | Resolved and covered by host fault injection |
| SS-03 | High | Starvation | Priorities break BLE/Web progress on single-core ESP32-C3 | Scheduling resolved; physical C3 soak pending |
| SS-04 | High | TOCTOU/race | Config/NVS/Wi-Fi can start concurrently with a physical cycle | Resolved with fail-open maintenance lease |
| SS-05 | High | Functional safety | “Connected” does not require a first sample and auto-stop does not require fresh weight | Resolved with freshness, sequence, range, and slew checks |
| SS-06 | High | Session race | Any authenticated session keeps another session's Web cycle alive | Resolved with owning session and lease |
| SS-07 | Conditionally high | Security | Remote actuation over HTTP and shared factory password | Mitigated: unique password and remote actuation OFF by default |
| SS-08 | Medium | State loss | Accepted persistence/commands can be lost without end-to-end acknowledgement | Resolved with request ID, terminal state, and retained retry |
| SS-09 | Medium | Blocking/loss | Critical BLE event waits forever and remote STOP is not retried | Resolved with overwrite mailbox and bounded retry |
| SS-10 | Medium | Recovery | Initial AP/HTTP failure has no retry | Resolved with rollback and bounded backoff |
| SS-11 | Medium | Supervision | Watchdog/progress monitoring does not cover every task and resource | Resolved in firmware; HIL fault injection pending |
| SS-12 | Medium | Initialization | EEPROM, BLE, and CPU-frequency results are ignored | Resolved with degraded/fatal-no-close modes |
| SS-13 | Low | Persistence | Normal read-back, canonical layout, and write policy can improve | Read-back resolved; wear subject to measurement |
| SS-14 | Low | Resources/operation | Missing stack/heap/latency telemetry; C3 uses 96% of flash | Telemetry and CI budget implemented; soak pending |

## Critical blockers

### SS-01 — Critical — CN9 protections are not independent

**Evidence.** Both timers were created with `ESP_TIMER_TASK` in
[shotStopper.ino](../../shotStopper/shotStopper.ino#L455), ran on the same SoC,
and controlled the same GPIO/driver/relay. There was no contact or real CN9
voltage reading; `relayClosed` reflected only the internal flag and commanded
output. The “independently opens CN9” comment meant independence from the
normal loop, not hardware independence.

Espressif documents that `ESP_TIMER_TASK` callbacks can be delayed when
higher-priority tasks or a flash operation prevent the timer task from running:
[ESP Timer, Task Dispatch](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html).

**Possible failure.** Total SoC lockup, partial brownout, scheduler corruption,
stalled flash/cache, a stuck GPIO, shorted transistor, or welded contact could
keep CN9 closed. Both timers shared several failure modes and did not provide
independent redundancy.

**Required fix.** Add a barrier outside firmware:

- an electrically fail-open relay/driver and defined pull-down from power-up;
- an external monostable/watchdog requiring periodic pulses to permit CN9 to
  remain closed, with a physical maximum duration;
- contact/voltage feedback independent from the commanded GPIO;
- when the risk analysis requires it, the machine's own certified thermal or
  pressure cutoff, never replaced by this controller.

The external watchdog must open even with a stopped CPU and must not be fed by
an ISR/task that remains alive while logical control is dead. Validate a welded
relay, shorted transistor, reset, brownout, and clock loss. Moving a timer from
TASK to ISR dispatch does not replace this recommendation.

### SS-02 — Critical — timer-free window between arming and closing

**Evidence.** [setCn9Closed(true)](../../shotStopper/shotStopper.ino#L485):

1. armed the hard and operational timers at lines 500–516;
2. only then entered `relayMux`, published timestamps, energized the relay, and
   set `cn9Closed=true` at lines 518–525.

Callbacks opened only when `cn9Closed` was already true. This interleaving was
possible:

1. the task arms the one-shots;
2. it is suspended for longer than the deadline, for example by a
   higher-priority BLE transport spin;
3. callbacks execute, observe `cn9Closed == false`, return, and consume their
   one-shots;
4. the task resumes and closes CN9 without an active timer.

The 5-second operational minimum made this unlikely during healthy operation,
but a safety limit must be correct precisely during long stalls. On ESP32-C3,
ArduinoBLE's HCI creates a priority-5 task containing a spin without yield while
control runs at priority 3, so the interleaving was not merely theoretical.

**Minimum recommended fix.** Implement a transactional protocol with generation
and state (`OPEN`, `ARMING`, `CLOSED`, `TRIPPED`):

- publish generation/deadline before starting timers;
- make the callback invalidate that generation even if the GPIO has not closed;
- immediately before and after energizing, verify that the generation remains
  armed and the deadline has not expired;
- if any step fails or has expired, write OPEN unconditionally;
- identify old callbacks so they cannot affect a new cycle.

Moving one line without testing every preemption point is insufficient. Create
a deterministic test that fires the callback at every point between
`start_once()` and `digitalWrite(CLOSED)`.

## High-severity findings

### SS-03 — High — reproducible ESP32-C3 priority starvation

**Evidence.** The BLE worker was created at priority 2 in
[shotStopper.ino](../../shotStopper/shotStopper.ino#L1076), the network manager
at priority 1 in
[ShotStopperNetwork.cpp](../../shotStopper/ShotStopperNetwork.cpp#L197), and
`loopTask` was raised to priority 3 at the end of setup in
[shotStopper.ino](../../shotStopper/shotStopper.ino#L1787).

The installed SDK sets `CONFIG_FREERTOS_UNICORE=1` for C3. Arduino ESP32 core
3.3.3 calls `vTaskDelay(5)` in `yieldIfNecessary()` only after more than
2,000 ms; the stopper loop did not block by itself. Priority 3 could therefore
run almost continuously while priority-2/1 tasks received tiny windows roughly
every two seconds.

This was especially serious during `scale.init()`: its scan had a 100 ms wall
deadline but might run only a few milliseconds before preemption and resume
after the deadline. The Web/network manager could appear completely down while
local control remained active and fed the loop WDT.

**Recommended fix.** Add explicit, bounded periodic yielding in `loop()`
(`vTaskDelay(1)` or an event wait), or redesign priorities per target. Before
choosing values:

- measure p99/p99.999 paddle-to-GPIO-OPEN latency;
- verify that the external SS-01 timer covers all scheduling behavior;
- measure each task's runtime and minimum progress on C3;
- ensure Wi-Fi/BLE and Idle are never starved.

Do not simply raise all other priorities: Espressif warns that overly high user
task priorities can destabilize Wi-Fi/BT. The criterion is bounded progress for
every task, not merely making the loop highest priority.

### SS-04 — High — the Ready validation does not reserve state

**Evidence.** HTTP handlers inspected a snapshot, the loop revalidated and
forwarded the command, and the network manager checked
`controlAllowsNetworkMutation()` again in
[ShotStopperNetwork.cpp](../../shotStopper/ShotStopperNetwork.cpp#L708). No
check acquired a reservation preventing the physical paddle from starting a
cycle immediately afterward. Between `xQueueReceive`, the second validation,
and `processAcceptedCommand`, the other core could enter `beginCycle()`.

**Possible failure.** An NVS write, clear/factory reset, AP restart, scan, or
Wi-Fi change could begin during a newly started cycle. Flash operations could
delay `ESP_TIMER_TASK` callbacks exactly when CN9 needed to open on time.
Configuration could also be persisted in an unexpected order.

**Recommended fix.** Implement a maintenance lease owned by the control task:

1. network requests a mutation;
2. control enters a state that prevents new physical/Web cycles and confirms
   paddle OFF, CN9 OPEN, and timers stopped;
3. network performs the generation-identified operation;
4. it returns a result and control releases the lease.

A new paddle action during the reservation must cancel/reject the mutation and
keep CN9 open, not run concurrently. Test the transition at every instruction
on two cores.

### SS-05 — High — automatic operation without a first or fresh sample

**Evidence.** `beginCycle()` decided `startedWithScale` using only `CONNECTED`
state and worker progress in
[shotStopper.ino](../../shotStopper/shotStopper.ino#L1182). That progress was
updated every iteration even with no weight. AcaiaArduinoBLE did not expire a
connection that never received its first packet. Furthermore,
[automaticScaleStopDue()](../../shotStopper/shotStopper.ino#L1295) did not
enforce a maximum age for the latest weight; the library could wait 5 seconds
after the last sample before declaring timeout. Web status calculated freshness,
but the automatic decision did not use it.

**Possible failure.** A cycle could be marked automatic without a valid sample
or extrapolate from stale/discarded data. A corrupted but finite frame also
entered regression; `recordWeightSample()` had no range, slew, or monotonicity
filter beyond `isfinite()`.

**Recommended fix.** Enabling and maintaining automatic operation must require:

- at least one recent valid sample before startup and one new cycle sample;
- a strict maximum age at the instant auto-stop is decided;
- a sequence newer than startup and any reconnect;
- a physical range and maximum slew, with outliers degrading to manual;
- parser validation according to A-05 in the library audit.

When uncertain, rely on timing/paddle limits and mark the cycle manual. Never
interpret an invalid weight as zero.

### SS-06 — High — Web heartbeat has no cycle owner

**Evidence.** `serviceSessions()` selected the latest heartbeat from **any**
active session in
[ShotStopperNetwork.cpp](../../shotStopper/ShotStopperNetwork.cpp#L626).
`CycleSession` stored only `source=WEB`, not the index/token/generation of the
session that started the cycle.

**Possible failure.** Session A could start a Web paddle cycle and disappear;
session B's continuing heartbeat prevented A's safety STOP. Session replacement,
logout, or owner expiry could not be associated with the correct cycle.

**Recommended fix.** Issue a non-reusable `controlLeaseId` bound to
session+generation when accepting `PADDLE_ON`/rinse. Only the owning lease's
heartbeat may keep the cycle alive. Owner logout, replacement, expiry, or
credential change must enqueue an idempotent STOP. Another session may issue
STOP but cannot renew the lease.

### SS-07 — Conditionally high — remote channel unsuitable as a safety boundary

**Evidence.** The factory password was shared (`Micra1234`) in
[ShotStopperPersistence.h](../../shotStopper/ShotStopperPersistence.h#L20), STA
kept HTTP active, and sessions/tokens traveled without TLS. An authenticated
session could close CN9 through Web paddle/rinse. Rate limiting, CSRF, random
tokens, and bounded bodies did not protect against observation/manipulation
inside the network or known factory credentials.

**Possible failure.** A LAN or AP actor could obtain credentials/tokens or use
the default password and actuate the machine without physical presence. Impact
depends on real network isolation and whether unsupervised operation is possible.

**Recommended fix.** Define a threat model before selecting technology. At a
minimum:

- use a unique per-device password and require changing it on first use;
- disable remote CN9 closing by default or require short-lived local physical
  enablement;
- do not expose the controller to untrusted networks; segment STA;
- if remote control remains on a hostile network, use a viable authenticated
  and encrypted channel with replay protection.

Adding TLS to the ESP32 in isolation is not recommended without measuring
memory and certificate management; physical authorization may be a simpler and
more robust barrier.

## Medium-severity findings

### SS-08 — Medium — acceptance does not imply application or persistence

**Evidence.** Handlers returned HTTP 202 after enqueueing. The loop could apply
`runtimeConfig`, then fail to forward `PERSIST_RUNTIME` if the second queue was
full in [shotStopper.ino](../../shotStopper/shotStopper.ino#L1468). An NVS
failure was only logged by the network manager; the original client received no
result. In `processAcceptedCommands()`, if safety changed after dequeue, code
called `xQueueSendToFront(..., 0)` and ignored the result in
[ShotStopperNetwork.cpp](../../shotStopper/ShotStopperNetwork.cpp#L708). Another
producer could fill the slot and silently lose the command.

**Possible failure.** The UI could display an accepted change, runtime could use
it, and the old value could return after reboot. Factory/network/restart might
not occur despite a 202 response. An automatic offset update might also fail to
persist.

**Recommended fix.** Carry a `requestId` to an observable terminal result
(`queued`, `applied`, `persisted`, `failed`). Retain a dirty/coalesced revision
and retry it with backoff; do not depend on reinserting a dequeued element. Do
not report `restarting:true` for destructive commands until factory reset is
confirmed.

### SS-09 — Medium — infinite critical-event wait and STOP without retry

**Evidence.** `publishScaleEvent(event, true)` used `portMAX_DELAY` in
[shotStopper.ino](../../shotStopper/shotStopper.ino#L836). If the queue stopped
draining, the BLE worker stopped updating progress. `requestRemoteTimerStop()`
set `stopTimerRequested=true` before checking availability/queue and did not
retry a failure in [shotStopper.ino](../../shotStopper/shotStopper.ino#L633).

**Impact.** This did not keep CN9 closed—the physical path opened first—but it
could freeze the worker, prevent reconnection, and leave the scale timer
running. This was an availability/consistency issue, not the machine's primary
safety limit.

**Recommended fix.** Reserve an overwrite mailbox/slot for critical results or
use a finite timeout and loss counter. Make STOP idempotent with bounded retry
while the same connection/generation exists. Do not block the BLE owner merely
to report a result.

### SS-10 — Medium — no recovery from initial AP/HTTP failure

**Evidence.** `startNetwork()` set `startupComplete_=true` before calling
`startFallbackAccessPoint()` in
[ShotStopperNetwork.cpp](../../shotStopper/ShotStopperNetwork.cpp#L332). If
`softAP`, configuration, or `httpd_start` failed, state remained inactive and
the initial path did not retry. STA reconnection had more retry logic, but the
initial fallback did not.

**Recommended fix.** Use a state machine with bounded backoff for AP and HTTP,
a failure counter, and Serial/local recovery. Every partial startup must perform
idempotent rollback before retrying.

### SS-11 — Medium — incomplete supervision

**Evidence.** `enableLoopWDT()` supervised the loop, and the loop inspected BLE
progress. There was no health deadline for the network manager/HTTP or
persistence; a priority-3 loop starving lower-priority tasks still fed its WDT.
The BLE worker was not directly subscribed to TWDT either.

**Recommended fix.** Maintain a per-task/resource health table with deadlines
and staged recovery: degrade to manual, restart a peripheral/task only when
safe, then perform a controlled reset. Record `esp_reset_reason` and the last
durable state. Follow the official
[Task Watchdog](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/system/wdts.html)
guidance; never feed a watchdog from a task other than the task being monitored.

### SS-12 — Medium — ignored initialization results

**Evidence.** [setup()](../../shotStopper/shotStopper.ino#L1713) ignored
`setCpuFrequencyMhz(80)`, `EEPROM.begin()`, and `BLE.begin()`. In
ArduinoBLE/ESP32 2.1.0, `BLE.begin()` itself reported success even when several
internal resources failed.

**Possible failure.** Code could read uninitialized EEPROM for migration,
announce BLE as active when it was not, or run at a frequency different from
the qualified value. The machine still needed to remain fail-open, but diagnosis
and recovery were ambiguous.

**Recommended fix.** Check every result and establish explicit modes:
fatal-no-close when timers/relay GPIO fail, local-manual when BLE fails, and
local without Web when network/persistence fails. Skip EEPROM migration when
`begin()` fails. BLE also requires the A-07 dependency fix because its current
return value is unreliable.

## Improvements conditional on measurement

### SS-13 — Low — durable persistence and wear

**Positive current state.** Two alternating slots, magic/schema/size/checksum,
modular revision selection, explicit migration, and factory reset with
read-back provide a solid base. NVS is designed for power-loss recovery and
wear leveling:
[official NVS documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html).

**Targeted improvements.** Normal save checked bytes written but did not reread
the slot; factory reset did. The current struct was serialized using native C++
layout, which is sensitive to future padding/ABI changes. Every valid offset
change caused a write, however small.

Recommendations:

- perform normal read-back and checksum verification before declaring
  `persisted`;
- use a canonical fixed-width field format for the next schema without
  retrospectively rewriting healthy records;
- coalesce offsets by epsilon/time if metrics show meaningful write volume;
- expose write/failure counters and test power interruption at every phase.

There is no evidence of imminent wear under normal domestic use; do not change
policy on intuition alone.

### SS-14 — Low — long-duration margins and observability

**Evidence.** C3 compiled to 96% of flash while global RAM remained at 15%.
BLE and network tasks reserved 8,192-byte stacks without publishing high-water
marks. Minimum heap/largest block, maximum loop/timer latency, per-task runtime,
and brownout/reset cause were not recorded. `ShotTrajectory` reserved about
8 KiB for 1,000 samples even though regression used the latest 10; measured RAM
still had margin, so this was not a current defect.

**Recommendation.** Add low-cost production telemetry and release thresholds.
Simplify buffers/Strings only when measured heap, stack, or flash requires it.
Set a C3 flash budget in CI—for example, fail before the physical limit to
retain diagnostic/update headroom—without blindly removing features.

## Reviewed defenses worth retaining

- Relay GPIO driven OPEN before Serial, EEPROM, and BLE in
  [shotStopper.ino](../../shotStopper/shotStopper.ino#L1713).
- Normally-open relay and de-energized CN9-open state.
- `setCn9Closed(true)` rejects closing when timers fail.
- Operational limit validated between 5,000 and 60,000 ms, with a 60,000 ms
  global limit.
- Short timer callbacks without Serial, allocation, or blocking waits.
- BLE isolated from the relay loop; loss/staleness degrades the cycle to manual.
- Per-cycle configuration snapshots; Web changes do not alter an active cycle.
- Timing subtraction mostly uses `uint32_t`, correct across `millis()` wrap.
- Bounded HTTP bodies, sockets, sessions, queues, scans, and logs.
- JSON validation rejects duplicate/unknown fields and invalid types/ranges.
- Random tokens, CSRF, constant-time comparison, and rate limiting reduce
  trivial attacks.
- Dual persistence with CRC/revision and test-covered migrations.
- Short local locks with no observed nested acquisition cycle forming deadlock.

## Verification performed during the original audit

### Host tests and sanitizers

Baseline from `shotStopper/tests/run_host_tests.sh` during the audit:

- 79 control/race/Web tests: 0 failures;
- the same suite with ASan+UBSan: 0 failures;
- 14 persistence tests: 0 failures;
- persistence with ASan+UBSan: 0 failures;
- valid embedded JavaScript, 17 verified routes;
- no incompatible legacy paths.

Post-remediation verification runs 93 control/race/Web tests, 16 persistence
tests, and 4 external CN9 interface tests, all in normal and ASan+UBSan modes,
plus the check proving that remote actuation is disabled in the default build.

The subsequent Arduino matrix compiles base and external-safety + opt-in remote
control variants on all three targets. The largest `.bin` is the opt-in
ESP32-C3 build at 1,285,008 bytes against a 1,300,000-byte CI budget. Each target
map places `independentSafetyTimerCallback` in `.iram1`.

Instrumented coverage:

| Component | Regions | Lines | Branches | Functions |
|---|---:|---:|---:|---:|
| `shotStopper.ino` | 86.06% | 85.70% | 73.24% | 98.63% |
| `ShotStopperDomain.h` | 90.08% | 47.94% | 73.00% | 64.71% |
| `ShotStopperPersistence.h` | 89.01% | 87.37% | 66.88% | 100% |

Host tests do not exercise real FreeRTOS scheduling, radio, flash with suspended
cache, `esp_timer`, GPIO, or the ArduinoBLE lifecycle. SS-02 through SS-06 could
therefore coexist with a green suite.

### Builds with maximum warnings

Arduino CLI, Espressif core 3.3.3, ArduinoBLE 2.1.0, isolated outputs under
`/private/tmp`:

| Target | Result | Flash | Global RAM |
|---|---:|---:|---:|
| ESP32 Dev Module | OK | 1,214,375 B / 92% | 62,008 B / 18% |
| ESP32-C3 Dev Module | OK | 1,266,280 B / 96% | 50,024 B / 15% |
| ESP32-S3 Dev Module | OK | 1,154,991 B / 88% | 58,476 B / 17% |

Stopper-owned code produced no relevant warnings. AcaiaArduinoBLE produced the
eight critical A-01 warnings. Core headers generated initializer warnings on
C3/S3 that should be tracked during toolchain upgrades but are not attributed
to this firmware.

## Original remediation plan — retained for traceability

The software points in this plan are implemented as stated in the status table
at the start of the document. Physical and HIL tests remain release criteria,
not tasks firmware can declare complete.

### Before energizing an unsupervised machine

1. Implement SS-01: an independent physical limit and real feedback.
2. Fix SS-02 with a generation protocol and a test at every preemption point.
3. Fix A-01/A-03/A-07 in the BLE library or disable BLE automation.
4. Resolve SS-03 on C3 and demonstrate bounded progress for control, BLE,
   network, timer, and Idle.
5. Require weight freshness/integrity according to SS-05.

### Before enabling Web control/persistence

6. SS-04 maintenance lease.
7. SS-06 owning session lease and SS-07 physical authorization/threat model.
8. Durable SS-08 acknowledgement/retry and SS-10 AP/HTTP retry.

### Only after measurement

9. SS-11/SS-14 supervision and telemetry.
10. SS-13/SS-14 persistence/memory optimizations when data demonstrates need.

## Minimum release-test matrix

- **Timer/relay:** pause control at every arming point; block HCI; suspend
  flash; trigger both deadlines simultaneously; reboot/brownout; disconnected
  relay and welded contact.
- **Scheduling:** single-core C3 and dual-core ESP32/S3 with saturated Wi-Fi,
  continuous BLE scan, slow HTTP, and congested Serial; measure paddle-to-OPEN
  and deadline-to-OPEN latency.
- **BLE:** 72 hours with scale power cycles, out-of-range conditions, stopped
  notifications, corrupted frames, and repeated reconnects.
- **Persistence:** power cut/failure emulation before, during, and after every
  put/commit/clear; corrupted A/B slot; revision wrap.
- **Web:** two sessions, owner replacement/logout/expiry, replay, full queue,
  and physical paddle transitions exactly between every validation and mutation.
- **Resources:** seven-day soak with minimum heap/largest block, stack
  high-water marks, per-task runtime, resets, and late-callback count.
- **Hardware safety:** use risk analysis to prove that one fault cannot keep the
  boiler/flow in a dangerous state; firmware must not be the only layer.

## Limitations

- Static and host audit; no real Micra was connected and CN9 was not instrumented
  with a logic analyzer.
- EMI, real bounce, brownout, power supply, temperature, relay behavior, and
  mechanical faults were not measured.
- No multi-day physical soak or scheduler/HCI fault injection was performed.
- The tree was modified; results describe this exact working tree, not
  necessarily a reproducible tag.

Release criteria must be based on hardware test evidence and a formal risk
assessment. Crash-free firmware alone does not make a system safe against
steam, pressure, and thermal energy.

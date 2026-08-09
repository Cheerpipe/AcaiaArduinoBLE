# AcaiaArduinoBLE audit remediation

This file tracks the implementation status of the findings in
`ACAIA_ARDUINO_BLE_ROBUSTNESS_AUDIT.md`. It does not replace hardware-in-the-loop or
safety validation.

| Finding | Status | Implemented response |
|---|---|---|
| A-01 | Resolved in this library | Remote characteristics are retained through copy construction, never copy-assigned, and are destroyed before peer teardown. The owner object is non-copyable. |
| A-02 | Resolved | Debug output uses the exact `readValue()` result and the packet buffer holds the maximum supported 20-byte frame. |
| A-03 | Resolved | All failure, timeout, write-error, remote-loss and explicit-disconnect paths converge on one idempotent `resetConnection()`. |
| A-04 | Resolved | A connection must deliver its first valid packet within `FIRST_PACKET_TIMEOUT_MS`; later availability uses only the last valid packet. |
| A-05 | Substantially hardened | Exact lengths/read counts, known Acaia header/opcode/length/checksum, exponent bounds, Felicita digits/sign, generic framing/sign, finite values and a 10 kg absolute bound are checked. Unknown protocol checksums were not invented. Slew/outlier policy remains a consumer concern because acceptable dynamics depend on the application. |
| A-06 | Resolved | Every field is initialized; timestamps use `uint32_t` modular subtraction and the first Acaia heartbeat is explicitly due immediately. |
| A-07 | External blocker | ArduinoBLE 2.1.0 is pinned, but its [ESP32 HCI transport](https://github.com/arduino-libraries/ArduinoBLE/blob/2.1.0/src/utility/HCIVirtualTransport.cpp) still has unbounded waits and spin loops. The [official upstream implementation](https://github.com/arduino-libraries/ArduinoBLE/blob/master/src/utility/HCIVirtualTransport.cpp) still contained these operations when remediation was performed. A validated hardened ArduinoBLE fork or alternative backend is required before claiming bounded BLE progress. |
| A-08 | Mitigated | Scan-start failure and scan timeout are distinct, the scan window is three seconds, scan is always stopped, and the synchronous loop yields every millisecond. Connect/discovery remain synchronous because ArduinoBLE exposes them that way. |
| A-09 | Resolved | Methods enforce capabilities internally. `tareStartTimer()` is generic-only and `beep()` can no longer tare Acaia or send an Acaia command to Felicita. |
| A-10 | Resolved as an API contract | The class is explicitly single-owner, non-copyable and documented as not thread-safe. A mutex was not added around potentially blocking dependency calls. |
| A-11 | Implemented where actionable | Version 3.4.0 pins ArduinoBLE 2.1.0, commands are immutable, telemetry is exposed, host tests cover lifecycle, timeout, parser corpus and 10,000 reconnect cycles, and CI compiles ESP32/C3/S3 with core 3.3.3. |

## Automated evidence

`libraries/AcaiaArduinoBLE/tests/run_host_tests.sh` compiles the real library source against a fake
ArduinoBLE backend where `BLECharacteristic::operator=` is deleted. It enables
`-Werror`, `-Wdeprecated-copy`, AddressSanitizer and UndefinedBehaviorSanitizer.
The suite covers:

- scan/connect/discovery/subscribe/initialization-write failures;
- command-write and remote disconnect cleanup;
- first-packet and steady-state valid-packet deadlines;
- 13- and 17-byte Acaia packets, checksum/exponent corruption and truncated
  reads;
- Felicita ASCII validation and generic/old packet bounds;
- notification lengths from 0 through 64 bytes; and
- 10,000 connect/disconnect/reconnect cycles.

Firmware compilation should continue to use ESP32 core 3.3.3, ArduinoBLE
2.1.0 and `--warnings all` for ESP32, ESP32-C3 and ESP32-S3. Absence of compile
warnings and passing host tests do not resolve A-07 or replace RF/power-loss
hardware testing.

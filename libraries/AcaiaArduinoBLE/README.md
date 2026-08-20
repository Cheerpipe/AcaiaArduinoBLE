# AcaiaArduinoBLE
Acaia / Bookoo / Felicita / AtomHeart Eclair Scale Gateway using the ArduinoBLE library for esp32-based devices.
This is an Arduino Library which can be found in the Arduino IDE Library Manager.

## Scale Compatibility

| Mfr    | Model   | Submodel | Firmware | Connection Performance | Auto-Tare | Auto-Start/Stop Timer | Auto-Reset Timer |
| ------ | ------- | ------- |------- |------ | ------ |------ |------ |
| Acaia  | Lunar   | USB-Micro <br>(Pre-2021) | v2.6.019 | Great | Yes | Yes | Untested
| Acaia  | Lunar   | USB-C <br> (2021 version)  | v1.0.016 | Hit or Miss    | Yes | Yes | Yes
| Acaia  | Pearl S | USB-Micro                  | v1.0.056 | Ok    | Yes | Yes | Yes
| Acaia  | Pearl S | USB-C                      | ----     | Ok    | Yes | Yes  | Yes
| Acaia  | Pyxis   | ----                       | v1.0.022 | Good  | Not Recommended (too sensitive) | Yes | Yes
| Bookoo | Themis  Mini | ----                       | v1.0.5   | Great | Yes | Yes | Yes
| Bookoo | Themis Ultra  | ----                 | ----   | Great | Yes | Yes | Yes
| Felicita | Arc   | ----                       | ----   | ---- | Yes | Yes | Yes
| AtomHeart | Eclair | ----                      | v2.1.0 | Testing | Yes | Yes | Yes


## Requirements

This library is intended for Arduino devices compatible with
[ArduinoBLE](https://www.arduino.cc/reference/en/libraries/arduinoble/).
Release 3.5.0 is compiled and tested against ArduinoBLE 2.1.0, which is pinned
in `library.properties` so upgrades cannot silently change the audited BLE
lifecycle behavior.

Shot Stopper's `scripts/patch_arduinoble.sh` bounds ESP32 VHCI / HCI ACL / ATT
indication waits (1 s deadlines; see patch
`ArduinoBLE-2.1.0-hci-bounded-waits.patch`). Combined with
`BLE.setTimeout(BLE_OPERATION_TIMEOUT_MS)`, host-side waits return instead of
blocking forever. This library is still not a standalone safety mechanism:
pair it with task watchdogs and fail-open outputs. See
[Audit remediation](../../docs/audits/AUDIT_REMEDIATION.md) for residuals and
the required hardware/soak validation (M72/M73).

On Arduino-ESP32 **3.3.6+**, `initArduino()` releases BLE controller RAM unless
a linked translation unit includes `esp32-hal-alloc-ble-mem.h`. Native
`BLE`/`SimpleBLE` do; ArduinoBLE does not. This tree includes the header
because Shot Stopper is pinned to **3.3.11** and requires ESP32-S3 with PSRAM
so Web UI buffers can live in SPIRAM while the BLE controller stays on
internal SRAM. After a core bump, run the BLE+Web UI hardware gate
(M72 smoke, M73 short soak) in the Shot Stopper manual test plan.

## Robust connection behavior

Version 3.5.0 adds a one-second ArduinoBLE operation timeout and forces a
recoverable disconnect after eight consecutive invalid notifications. It also
retains the explicit ownership and cleanup for remote characteristics, the
five-second first-valid-packet deadline, validation before a packet refreshes
availability, a three-second blocking `init()` scan deadline with distinct
failure reasons, and connection telemetry. Idle discovery uses `startScan()` /
`pollScan()` so the owner task can keep calling `BLE.poll()` and feeding its
watchdog; the idle GAP scan stays enabled until a match or a filter change.
`init()` remains the blocking helper for sketches and still stops at
`SCALE_SCAN_TIMEOUT_MS`. Neither path calls `BLE.begin()` or `BLE.end()`.
`startScan()` does not restart an already-active GAP scan with the same filter;
it does stop and restart when the MAC/name filter changes or `forceRestart` is
set. Idle scans request `withDuplicates=true` so a missed first advertisement
is not dropped for the rest of the session. A non-empty `startScan(mac)` still
runs a **name scan** (`BLE.scan`), but only GATT-connects when the address
matches; other compatible advertisements are exposed via
`takeSeenAdvertisement()` for history without connecting. Name scans without a
filter still require a known scale prefix; a connect-filter MAC may connect
even when the advertisement has no local name.

Stock ArduinoBLE 2.1.0 scans active at 20/20 ms. Shot Stopper patches GAP to
active 40/20 ms (50% duty) via `scripts/patch_arduinoble.sh` so SCAN_RSP names
stay visible while leaving airtime for Wi-Fi. The same script also applies an
OOM-safe discovery patch, a BLE-host PSRAM allocator, and HCI bounded waits:
advertising reports, linked-list nodes, remote ATT trees, local GATT 1800/1801,
and characteristic value buffers allocate with `BLEHostAlloc` (SPIRAM first,
internal fallback) + placement `new` so small host objects skip the
ALWAYSINTERNAL DRAM threshold and a failed alloc drops the advert instead of
`abort()` via `bad_alloc`. VHCI RX/TX stream ops, ACL credit waits, and ATT
indication confirms use ≤1 s deadlines (or the configured ATT timeout). The
VHCI stream buffers and `bleTask` stack stay in internal SRAM. Idle discovery
keeps that scan enabled until a match or filter change; `startScan()` /
`pollScan()` do not cycle GAP every 1 s or 3 s. GATT connect is stepped across
`pollScan()` calls so the owner task can feed its watchdog between ATT ops.
Migrating to ESP-IDF NimBLE is out of scope: this library stays on the pinned
ArduinoBLE lifecycle.

`AcaiaArduinoBLE` is a single-owner object: create it, call it, and destroy it
from one task only. It is intentionally non-copyable and is not thread-safe.
Call `disconnect()` before transferring BLE ownership to another component.

Useful diagnostics are available through:

- `isScanning()`
- `lastDisconnectReason()` / `lastDisconnectReasonName()`
- `lastValidPacketAgeMs()` (`UINT32_MAX` until the first valid packet)
- `rejectedPacketCount()`
- `reconnectCount()`

Every command validates connection and protocol capability internally. The
legacy `beep()` method no longer substitutes tare for sound; it now behaves
like `beepWithoutStateChange()` and succeeds only on Bookoo/generic scales.

Run the host lifecycle/parser suite with:

```sh
./libraries/AcaiaArduinoBLE/tests/run_host_tests.sh
```

When compiling the bundled example from a checkout, include the repository as
a library explicitly:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc --warnings all \
  --library libraries/AcaiaArduinoBLE shotStopper
```

## Printed Circuit Board
![shotStopperV3 screenshot](https://github.com/user-attachments/assets/a09fe8fb-3705-44c0-88a2-07c61d67b8f6)

The repository's main `shotStopper` firmware uses the ShotStopper PCB to make it simple to control your espresso machine using the scale. It is now the host application, rather than an Arduino library example.

A kit can also be ordered by visiting [tatemazer.com](https://tatemazer.com/store)

If you choose to build your own from scratch, v2.0 is recommended as it requires only through-hole components

Join the discord for updates and support: https://discord.gg/NMXb5VYtre

[![Video showing developmnent of the shotStopper](https://img.youtube.com/vi/434hrQDGtxo/0.jpg)](https://youtu.be/434hrQDGtxo)

## Espresso Machine Compatibility

| Model | Powered by Machine (5V) | Brew State Detection Method | Officially Documented |
| ----- | ----------------------- | --------------------------- | ---------------- |
| GS3 | No, requires included power supply | Solenoid Valve (Reed Switch) | Yes |
| Linea Micra | Yes | Brew Switch | Yes |
| Linea Mini* | Older, non-IoT machines may require a power supply | Brew Switch | Yes |
| Linea Mini R | Yes | Brew Switch | Yes |
| Silvia Pro (X) | Yes | Brew Button | Yes |
| Stone Espresso | Yes | Solenoid Valve (Reed Switch) | Yes |
| Ascaso Steel Duo PID | Untested | Brew Button | No
| Profitec Move | Yes | Brew Button | No |

*Ace Dotshot is compatible with the shotStopper. Also note, shot duration is automated at the scale with the shotStopper, making the dotShot redundant.

## Historical ShotStopper Configuration Notes

The following notes describe the upstream example from which the main
application evolved. They are retained for provenance; current configuration
is documented in the repository's [main README](../../README.md).

The following variables at the top of the shotStopper.cpp file can be configured by the user:

`MOMENTARY`
* true for momentary switches such as GS3 AV, Rancilio Silvia Pro, etc.
* false for latching switches such as Linea Mini/Micra, stone, etc.

`REEDSWITCH`
* true if a reed switch on the brew solenoid is being used to determine the brew state. This is typically not necessary so set to FALSE by default. This feature is only available for non-momentary-switches.

`AUTOTARE`
* true by default. The scale will automatically tare when the shot is started, and, if MOMENTARY is false, will perform another tare at 3 seconds to notify the user that the switch is latched and should be returned to the home position.
* if set to false, the shotStopper will never send a tare command. It is the user's responsibility to tare before each shot. This may be helpful if the scale is not stable when the shot begins, and thus the scale is unable to tare reliably.

`TIMER_ONLY`
* false by default. disables brew-by-weight functionality and enables only automatic timer and tare

Bookoo/generic scales also expose `setBeepLevel(0–5)` (0 mutes) and
`beepWithoutStateChange()`, which sends level 1. Both succeed only when that
protocol is detected; they never use tare as a substitute on Acaia, Felicita, or Eclair
scales.

AtomHeart Eclair supports tare and timer control, but its known protocol has
no independent beep, volume, mute, combined tare-and-start, or documented
audible command-feedback capability. Applications should route alerts to a
local buzzer when one is available instead of using tare as a sound command.

## Demo

You can find a demo on Youtube:

[![Video showing an shotStopper pulling a shot on a silvia pro](https://img.youtube.com/vi/oP3Cmke6daE/0.jpg)](https://www.youtube.com/shorts/oP3Cmke6daE)

## Scale Compatibility:

☑ Acaia Pyxis

☑ Acaia Lunar (usb-micro)

☑ Acaia Lunar 2021 (usb-c)

☑ Pearl S

☑ Felicita Arc

☑ Bookoo

☑ AtomHeart Eclair


## Bugs/Missing
1. Tare command is less reliable than pressing the tare button for pyxis
2. Only supports grams.

# Acknowledgement
This is largely a basic port of the  [LunarGateway](https://github.com/frowin/LunarGateway/) library written for the ESP32.

In addition to some minor notes from [pyacaia](https://github.com/lucapinello/pyacaia) library written for raspberryPI.

Felicita Arc support contributions from baettigp and A-TWJ

Bookoo contributions from philgood and same31

AtomHeart Eclair protocol information from AtomHeart-Lang

lunar 2019 contributions from jniebuhr

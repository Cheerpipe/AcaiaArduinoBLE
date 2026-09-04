# EspressoScaleBLE
Native ESP-IDF NimBLE gateway for espresso scales on ESP32-S3 devices with PSRAM.

Maintained by **Felipe Urzúa** (`cheerpipe@gmail.com`) as part of
[Cheerpipe/AcaiaArduinoBLE](https://github.com/Cheerpipe/AcaiaArduinoBLE).
The original AcaiaArduinoBLE library was created by Tate Mazer; see
[Acknowledgement](#acknowledgement).

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
| Decent | Scale / EspressiScale | ----              | ----   | Testing | Yes | No | No
| DiFluid | Microbalance / Ti | ----                 | ----   | Testing | Yes | No | No
| MyScale | KP2048B | ----                      | ----   | Testing | Yes | No | No
| Varia | AKU / Mini / Pro | ----                   | ----   | Testing | Yes | Yes | Yes
| Eureka | Precisa (`CFS-9002`, `LSJ-001`) | named GAP only | ---- | Testing | Yes | Yes | Yes
| WeighMyBru | ---- | ----                      | ----   | Testing | Yes | No | No

Not supported: Timemore Black Mirror DUO, Timemore Dot, Acaia Umbra, Eureka
Precisa units that advertise no GAP name (manufacturer-data only). Weight is
always reported in grams.

Scales without timer or volume bits follow the Eclair-like firmware path:
tare if present, local buzzer for alerts, no combined tare+start.


## Requirements

Release 5.0.0 uses the native NimBLE C APIs from ESP-IDF 5.5.x. The supported
integration is the IDF component in this repository, pinned to ESP-IDF 5.5.5
and Arduino-ESP32 3.3.11. It targets ESP32-S3 n8r4 and n16r8 boards; standalone
Arduino Library Manager and SAMD builds are no longer supported.

The BLE lifecycle is asynchronous and deadline-bounded. The runtime owns the
single NimBLE host, while the scale worker remains the sole owner of scale
state. Pair the library with task watchdogs and fail-open outputs; it is not a
standalone machine-safety mechanism. See
[Audit remediation](../../docs/audits/AUDIT_REMEDIATION.md).

## Robust connection behavior

Discovery, connection, GATT resolution, subscription and writes run through a
bounded native NimBLE state machine. Every asynchronous operation is tied to a
connection generation and deadline; late callbacks cannot revive a discarded
connection. Cleanup is idempotent, eight consecutive invalid notifications
force a recoverable disconnect, and the first-valid-packet and silence limits
remain protocol-specific.

Light / Normal / Aggressive scan presets retain their 25%, 50% and 100% duty
semantics. Fixed advertisement slots and fixed GATT handle storage avoid a
heap allocation per advertisement. Protocols that permit UUID-only discovery
still match without a GAP name; Varia and Eureka continue to require one. The
production host uses the hardware-qualified external NimBLE allocator, a 4096
byte internal host-task stack, MTU 96 and conservative fixed pools.

`EspressoScaleBLE` is a single-owner object: create it, call it, and destroy it
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
./libraries/EspressoScaleBLE/tests/run_host_tests.sh
```

Build the bundled firmware through `./scripts/build-idf`; see
[Build environment](../../docs/BUILD.md).

## Printed Circuit Board
The host application in this repository is Advanced Shot Stopper
([Cheerpipe/AcaiaArduinoBLE](https://github.com/Cheerpipe/AcaiaArduinoBLE)).
Hardware guidance is in the [main README](../../README.md) and [docs/](../../docs/).
This fork provides firmware and guidelines; it is not a commercial kit.

The original Shot Stopper PCB and kit were designed by Tate Mazer.

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
is documented in the [main README](../../README.md) and under [docs/](../../docs/).

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

☑ Decent Scale / EspressiScale

☑ DiFluid Microbalance / Ti

☑ MyScale (KP2048B)

☑ Varia AKU / Mini / Pro

☑ Eureka Precisa (named GAP)

☑ WeighMyBru


## Bugs/Missing
1. Tare command is less reliable than pressing the tare button for pyxis
2. Only supports grams.

# Acknowledgement
This library is maintained by Felipe Urzúa
([Cheerpipe/AcaiaArduinoBLE](https://github.com/Cheerpipe/AcaiaArduinoBLE)).
It derives from [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)
by Tate Mazer.

This is largely a basic port of the  [LunarGateway](https://github.com/frowin/LunarGateway/) library written for the ESP32.

In addition to some minor notes from [pyacaia](https://github.com/lucapinello/pyacaia) library written for raspberryPI.

Felicita Arc support contributions from baettigp and A-TWJ

Bookoo contributions from philgood and same31

AtomHeart Eclair protocol information from AtomHeart-Lang

Decent, DiFluid, MyScale, Varia, Eureka, and WeighMyBru protocol knowledge from
[gaggimate/esp-arduino-ble-scales](https://github.com/gaggimate/esp-arduino-ble-scales)
by [jniebuhr](https://github.com/jniebuhr) and contributors. Those protocols
were reimplemented here from that public reference; no source was copied.

lunar 2019 contributions from jniebuhr

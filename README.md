<p align="center">
  <img src="docs/advanced-shot-stopper.svg" alt="Advanced Shot Stopper" width="520">
</p>

# Advanced Shot Stopper

ESP32-S3 firmware for a **La Marzocco Linea Micra**. It adds brew-by-weight
and related workflow controls without replacing the machine’s own brew switch.

The controller reads the physical paddle on a GPIO and drives the machine’s
brew circuit through an isolated relay. A Bluetooth scale (designed first
for **Bookoo** Themis Mini / Ultra) supplies the weight. Other compatible
scales work through the vendored AcaiaArduinoBLE library.

This firmware is **dedicated to the Micra**. It is not a generic multi-machine
stopper.

> **Safety:** check isolation, polarity, and that the relay stays **open** on
> startup, reset, and power loss. Complete the
> [manual test plan](docs/MANUAL_TEST_PLAN.md) before connecting the machine.
> This project cannot make unsafe wiring safe. See the
> [Disclaimer](#disclaimer) — use at your own risk.

## Intro

This project is for people who want intelligent, reliable, safe, and advanced
brew-by-weight without changing the machine’s human-machine interface. No extra
buttons on the bar. You keep using the Micra with the paddle the same way you
always have.

This project started from
[tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE),
the original ESP32 firmware that stops an extraction by weight over Bluetooth.
Small Micra-specific changes grew into a dedicated rewrite: independent paddle
read and machine control, a walk-away workflow, and extra guards so a late cup, a
finger on the pan, or a dropped scale does not ruin the shot.

The original project proved that BLE brew-by-weight stop was possible.
Advanced Shot Stopper is now the main application in this repository. The
derived scale library remains here as a local dependency.

The day-to-day goal stays simple: put the cup down, flip the paddle, walk
away. The intelligence lives in firmware defaults, not in a new control panel.

## How it works

The Micra paddle does **not** connect to the brew circuit. It connects only between a
configured ESP32-S3 GPIO and GND. The relay COM/NO contact is the only
connection to that circuit. The firmware can therefore read the paddle and control the
machine independently.

That split is what makes the firmware “advanced”:

- Fine control of the brew workflow and its exceptions (rinse vs shot, late
  cup, missing scale).
- Weight noise filtering, so a bump or a noisy sample does not stop the shot.
- Anti-finger / accidental-touch protection at the start of an automatic shot.
- Intelligent handling when the scale drops or the stream goes stale.
- Guards for extractions that finish too fast or too slow.

Development used the ESP32-S3 1-channel relay board shown in
[`ESP32-S3_Relay_X1.png`](ESP32-S3_Relay_X1.png). The firmware’s default GPIO
map matches that board. Details are in [Hardware](docs/HARDWARE.md).

## Main features

### Brew by weight

When a usable scale is connected, the firmware closes the machine circuit with the paddle and
opens it at the target weight (minus a learned drip offset). You can turn
weight stop off and keep only the timer and tare. See
[Brew by weight](docs/features/brew-by-weight.md).

### Tare and retare

An automatic tare runs when the shot starts. If you put the cup down after
paddle ON, cup-presence detection can trigger a second automatic tare inside
the retare window—no button on the scale. See
[Tare and retare](docs/features/tare-retare.md).

### Cup protection

Late cup placement, a finger on the pan, or a bump at the start of the shot
should not cut the extraction. Retare, a start-of-shot protection window, and
cup-presence checks work together. In the Web UI these appear as **BBW
protection**, **Automatic retare**, **Cup**, and **Tare**. See
[Cup protection](docs/features/cup-protection.md).

### Fast extraction guard

If the target weight arrives too soon (often a coarse grind or channeling),
the shot can continue toward a recovery weight or a minimum brew time instead
of stopping thin. On by default. See
[Fast extraction guard](docs/features/fast-extraction-guard.md).

### Slow extraction guard

If the target has not arrived by a maximum brew time (often a fine grind),
the shot can cut at a floor weight instead of waiting for the full machine circuit limit.
On by default. See
[Slow extraction guard](docs/features/slow-extraction-guard.md).

### A→M time guard

If the scale is lost mid-shot, weight stop pauses and the firmware keeps
trying to reconnect. This guard still closes the machine circuit on a shorter, predictable
deadline so the shot does not run to the hard 60 s cap unnoticed. On by
default. See [A→M time guard](docs/features/auto-to-manual.md).

### Alerts

Beeps and an optional local buzzer mark tare, first drops, paddle-off
reminders, scale lost/connected, and extended-shot pulses. See
[Alerts](docs/alerts.md).

### Quick rinse

A short paddle ON→OFF (within the gesture window) is a timed group-head
rinse, not a shot. Machine circuit stays closed for the rinse duration, then opens.
Rinses are not stored in shot history. See
[Quick rinse](docs/settings/quick-rinse.md).

### Shot history

Finished shots are logged in the Web UI with goal, actual weight, duration,
flow, first drop, cut type, and stop detail. Export CSV or clear the log from
the same view. See [Shot history](docs/features/shot-history.md).

### Presets

Brew recipes live in presets (factory **Single** and **Double**, plus custom
copies). Target weight, BBW protection, Fast/Slow/A→M guards, and the learned
stop offset are per preset. Load, save, duplicate, or delete from
**Settings → Brew**. See [Presets](docs/features/presets.md).

## Technical features

### OTA

Update firmware over Wi-Fi without USB. CLI uses the device password as
`X-OTA-Token`; the Web UI uses the Admin unlock (change the password from
the factory default first). Dual-slot update with rollback if
the new image fails to serve the Web UI. See [OTA](docs/features/ota.md) and
[Build scripts](docs/SCRIPTS.md).

### Recovery mode

If Wi-Fi, Web UI, BLE, and USB are all unavailable, power on with the paddle
ON to enter a 60 s recovery window. Three `OFF→ON` cycles restore network
access; five do a factory reset. Machine circuit stays open. See
[Emergency recovery](docs/EMERGENCY_RECOVERY.md).

## Main settings

Each group is edited in the Web UI. Defaults are chosen so most people never
need to change them after first setup.

| Group | What it covers |
| --- | --- |
| **[Paddle](docs/settings/paddle.md)** | Paddle firmware only. Auto, Natural (default), or Original feel for the brew switch. Hidden on momentary. |
| **[Momentary](docs/settings/momentary.md)** | Momentary firmware only (with or without reed). Report running on press or on release. Hidden on paddle. |
| **[No-scale BBW](docs/settings/no-scale-bbw.md)** | Block a full automatic shot when brew-by-weight is on and the scale is missing. |
| **[Quick rinse](docs/settings/quick-rinse.md)** | Paddle firmware only. Short paddle ON→OFF becomes a timed rinse instead of a shot. Hidden on momentary. |
| **[Cup](docs/settings/cup.md)** | What counts as a cup placed or lifted. |
| **[Tare](docs/settings/tare.md)** | Automatic tare, late-cup retare, and settle time after tare. |
| **[Scales](docs/settings/scales.md)** | Preferred scale, drip delay, Bookoo volume and combined tare. |
| **[Alerts](docs/alerts.md)** | Sounds, output channel, paddle reminder, and the scale LED. |

## Admin

The Admin page is locked until you enter the device password (**Unlock administration**). The unlock stays active while that Admin page is open, or for 15 minutes after the last privileged action (Start/Stop, rinse, Wi-Fi, OTA). **Lock** (header or Admin) closes it immediately. USB serial does not ask for the password.

| Group | What it covers |
| --- | --- |
| **[Wi-Fi](docs/settings/wifi.md)** | Join your home network (STA), DHCP or static IP, first-boot fallback. |
| **Device password** | Single firmware password: SoftAP WPA2, OTA, and Admin unlock. Changed from **Admin → Device password** after unlocking. |
| **[AP](docs/settings/ap.md)** | Fallback access point `AdvancedShotStopperAP`. Uses the device password. |
| **[Factory reset](docs/settings/factory-reset.md)** | Erase settings, Wi-Fi, calibration, and shot history. |

## First connection

On a fresh flash or after factory reset:

| | Value |
| --- | --- |
| **Fallback Wi-Fi (AP) name** | `AdvancedShotStopperAP` |
| **Device password** | `ineedacoffee` (SoftAP WPA2 and OTA token) |
| **Web UI address (AP mode)** | `http://192.168.4.1` |

The password is case-sensitive. Join the AP, open the address above, then
claim the Web UI to save your home Wi-Fi. Step-by-step notes are in
[Wi-Fi](docs/settings/wifi.md) and [AP](docs/settings/ap.md).

## Documentation

**Features**

- [Brew by weight](docs/features/brew-by-weight.md)
- [Tare and retare](docs/features/tare-retare.md)
- [Cup protection](docs/features/cup-protection.md)
- [Fast extraction guard](docs/features/fast-extraction-guard.md)
- [Slow extraction guard](docs/features/slow-extraction-guard.md)
- [A→M time guard](docs/features/auto-to-manual.md)
- [Alerts](docs/alerts.md)
- [Quick rinse](docs/settings/quick-rinse.md)
- [Shot history](docs/features/shot-history.md)
- [Presets](docs/features/presets.md)

**Technical**

- [Firmware state machines](docs/STATE_MACHINES.md) — states, events, and how the FSMs interact
- [OTA](docs/features/ota.md) — Wi-Fi firmware update; scripts in [Build scripts](docs/SCRIPTS.md)
- [Emergency recovery](docs/EMERGENCY_RECOVERY.md) — paddle recovery mode

**Settings**

- [Paddle](docs/settings/paddle.md)
- [Momentary](docs/settings/momentary.md)
- [No-scale BBW](docs/settings/no-scale-bbw.md)
- [Quick rinse](docs/settings/quick-rinse.md)
- [Cup](docs/settings/cup.md)
- [Tare](docs/settings/tare.md)
- [Scales](docs/settings/scales.md)
- [Wi-Fi](docs/settings/wifi.md)
- [AP](docs/settings/ap.md)
- [Factory reset](docs/settings/factory-reset.md)

**Using and recovering the device**

- [FAQ](docs/FAQ.md)
- [USB serial CLI](docs/SERIAL_CLI.md)
- [Emergency recovery](docs/EMERGENCY_RECOVERY.md)

**Building and hardware**

- [Build environment](docs/BUILD.md) — macOS and Linux, from `git clone` to a flashable image. ESP-IDF only.
- [Build scripts](docs/SCRIPTS.md) — IDF commands and legacy Arduino-cli (unsupported).
- [Hardware](docs/HARDWARE.md) — development board, default GPIOs; BOM and schematic are TODO.

**For contributors**

- [Manual test plan](docs/MANUAL_TEST_PLAN.md)
- [Firmware state machines](docs/STATE_MACHINES.md)
- [Local AcaiaArduinoBLE library](libraries/AcaiaArduinoBLE/README.md)

## Disclaimer

**Use at your own risk.** Anyone who builds, installs, configures, or operates
firmware from this repository does so **under their sole responsibility**. The
authors and contributors **accept no liability** for any harm, loss, or damage
whatsoever — including but not limited to **personal injury, death, property
damage, equipment damage, business interruption, or psychological distress** —
arising from the use or misuse of this software, documentation, or any
derivative work.

You are solely responsible for:

- **Designing and building a correct, safe circuit** — suitable relay or contact,
  electrical isolation, ratings, polarity, feedback, and any external safety
  barrier (e.g. K2) appropriate for your machine and jurisdiction.
- **Installing and verifying that circuit** on your equipment, including bench
  tests and the full [manual test plan](docs/MANUAL_TEST_PLAN.md) before
  connecting to a live espresso machine.
- **Configuring the firmware correctly and safely** — including GPIO assignment,
  compile-time pin maps, polarity, machine circuit limits, and workflow parameters — so
  that paddle readback, machine control, and automatic stop behavior match your
  hardware. GPIO and other safety-critical pin assignments are **not**
  configurable from the Web UI; they must be set in source and verified at
  build time (see [Hardware](docs/HARDWARE.md) and the [FAQ](docs/FAQ.md)).

**Espresso machines are inherently hazardous.** A machine such as the La
Marzocco Linea Micra contains **pressurized boilers, hot water, and steam at high
temperature**. Adding automatic or remote control — including brew-by-weight
stop, relay actuation, Wi-Fi commands, and timer-based limits — can increase
risk if wiring, isolation, configuration, or software behavior is wrong.
Malfunction or misconfiguration could leave the brew circuit energized too long,
defeat intended safety interlocks, or cause scalding, flooding, electrical
hazard, or fire. **Do not connect this firmware to mains-powered espresso
equipment unless you understand these risks and have validated your entire
system on the bench first.**

This project provides software and documentation only. It **does not**
certify, warrant, or guarantee safe operation on any machine. No statement in
this repository should be interpreted as professional electrical, plumbing, or
machinery safety advice.

This project was developed with substantial assistance from artificial
intelligence tools. AI helped with design, implementation, documentation, and
testing workflows; human review, hardware validation, and safety judgment remain
the author’s responsibility. Use on real espresso equipment only after you have
verified wiring, isolation, and behavior on your own setup.

## Credits

The project retains the MIT license in [LICENSE](LICENSE).

Advanced Shot Stopper would not exist without
**[tatemazer](https://github.com/tatemazer)** and
[tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE).
That repository proved BLE brew-by-weight stop and shared the core scale
protocol work. This application firmware, Web UI, Micra paddle/machine circuit model, and
safety workflow are new work on top of that foundation.

The vendored library also credits:

- [LunarGateway](https://github.com/frowin/LunarGateway/) (frowin)
- [pyacaia](https://github.com/lucapinello/pyacaia) (lucapinello)
- Felicita Arc: baettigp, A-TWJ
- Bookoo: philgood, same31
- AtomHeart Eclair: AtomHeart-Lang
- Lunar 2019: jniebuhr

See the [library acknowledgement](libraries/AcaiaArduinoBLE/README.md#acknowledgement).

Runtime and tooling:

- [Espressif ESP-IDF](https://github.com/espressif/esp-idf)
- [Arduino-ESP32](https://github.com/espressif/arduino-esp32)
- [ArduinoBLE](https://github.com/arduino-libraries/ArduinoBLE)

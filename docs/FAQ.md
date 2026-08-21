# Frequently Asked Questions (FAQ)

Odd automatic behavior, lost access, and hardware questions. Defaults are a
fresh flash or a [factory reset](settings/factory-reset.md).

Feature and setting details (ranges, tables) live in the pages linked from
the [README](../README.md). This FAQ answers “why did that happen?”

## Shot and scale

| Question | Answer |
| --- | --- |
| **I cannot brew longer than 60 s. Why?** | A firmware **hard cap of 60 s** applies to every path that closes CN9. The Web UI cannot raise it. Separate from **Max BBW time**. See [Brew by weight](features/brew-by-weight.md). |
| **My shot stops around 50 s (or another value under 60 s).** | Check **Max BBW time** (default **50 s**). [A→M time guard](features/auto-to-manual.md) can also cut if the scale dropped. [Slow extraction guard](features/slow-extraction-guard.md) may decide at **44 s**; that is not a replacement for Max BBW time. |
| **The shot ended off-target and the scale was not stopping the flow.** | Often [A→M time guard](features/auto-to-manual.md) (on by default). If the scale is lost mid-shot, weight stop pauses, reconnect continues, and CN9 can still open on a deadline from shot start. Status shows `Off` / `Idle` / `Armed` / `A→M · …s`. |
| **The shot finished 2–4 g over target.** | Expected when [Fast extraction guard](features/fast-extraction-guard.md) is on and the target arrived **too soon**. The shot extends toward max recovery weight or minimum brew time. Turn the guard off if you do not want that. Pulses: [Alerts](alerts.md). |
| **The shot finished under target (for example 34 g) when it was slow.** | Expected when [Slow extraction guard](features/slow-extraction-guard.md) is on and the target was not reached by max brew time. BBW still wins if you hit target on time. |
| **The scale hit target but the shot did not stop.** | [Cup protection](features/cup-protection.md) / **BBW protection** (default **12 s**) blocks automatic weight stop at the start so a finger, late cup, or noise cannot cut. First drops can still beep. |
| **Can I put the cup down after I start?** | **Yes**, if **Automatic retare** is on (default). Place the cup in the **retare window** (default **4 s**). Putting the cup down (even with overshoot, ~150–200 g) is **not** first drop and does not block retare. A finger tap is also not first drop. Then **Post-tare grace** (default **2 s**) lets the scale settle before weight is used to stop. See [Tare](settings/tare.md) and [Cup](settings/cup.md). |
| **Why does it not stop exactly on the target grams?** | A **learned stop offset** (default 1.5 g, max 5 g) compensates for drip after CN9 opens. History shows `cut_type` and `stop_detail`, not a “prediction” type. See [Brew by weight](features/brew-by-weight.md). |
| **The scale disconnected mid-shot. What happens?** | Weight stop pauses; reconnect continues for the whole cycle. If three coherent samples return, BBW resumes. If [A→M](features/auto-to-manual.md) is on, CN9 may still open on the deadline. If A→M is off, the shot runs until paddle OFF or the 60 s wall. |
| **I turned Brew by weight off. Why no weight stop?** | Tare and the timer remain. Weight stop, retare, BBW protection, and offset learning do not. Cut is paddle, time limit, or remote **Stop**. Fast, Slow, A→M, and No-scale BBW become read-only on Home. |
| **BBW is on, scale is off, a long paddle does nothing (triple beep, CN9 open).** | [No-scale BBW](settings/no-scale-bbw.md) (*Avoid BBW shot without scale*, on by default). A long paddle does not close CN9. A short ON→OFF still rinses. The next start is a manual no-scale shot. |

## Network and access

| Question | Answer |
| --- | --- |
| **What are the default credentials?** | AP **`MicraShotStopperAP`** / **`Micra1234`**. Web UI in AP: **`http://192.168.4.1`**. See [First connection](../README.md#first-connection) and [AP](settings/ap.md). |
| **I cannot find the device Wi-Fi after first boot.** | With no home Wi-Fi saved, the AP stays up. With saved credentials, STA is tried first; SoftAP appears only if STA fails for ~15 s at boot. After a successful STA join, a later drop does **not** raise SoftAP — use USB `AP_START` or reboot. See [Wi-Fi](settings/wifi.md). |
| **How do I open the UI after I saved home Wi-Fi?** | `http://<device-ip>` from DHCP, your static IP, or serial logs at **115200**. If a new static IP is unreachable, settings revert in ~3 minutes. |
| **Can I change the password?** | **Admin → AP password** (current + new). USB: `SET_AP_PASSWORD` / `RESET_AP_PASSWORD`. See [USB serial CLI](SERIAL_CLI.md). |
| **I lost Wi-Fi or the UI password. How do I recover over USB?** | 115200 baud. `HELLO` replies `how are you` even if serial debug is off. `RESET_AP_PASSWORD`, `CLEAR_WIFI`, or `FACTORY_RESET`. Destructive commands need paddle OFF. |
| **No Web UI, Wi-Fi, BLE, or USB. How do I recover?** | [Emergency recovery with the paddle](EMERGENCY_RECOVERY.md). Power on with paddle ON: three cycles restore access; five do a factory reset. |
| **Can I control the shot from my phone?** | Monitoring always. Virtual paddle and remote rinse need a build with `SHOT_STOPPER_ENABLE_REMOTE_CN9=1`. **Stop** works in every authenticated build. The physical paddle always wins. |
| **The Web UI looks like garbage in `curl`.** | HTML is gzip. Browsers decode it. Use `curl --compressed http://<ip>/`. |
| **Serial shows `FT-PSK present but FT disabled, falling back to WPA2-PSK`.** | Expected. The home router advertises Fast Transition; the ESP32 uses WPA2-PSK. Not a failed join. |

## Hardware and compatibility

| Question | Answer |
| --- | --- |
| **Which boards can I use?** | ESP32-S3 with PSRAM only: **n16r8** (development board in [Hardware](HARDWARE.md)) or **n8r4**. Default pins: paddle GPIO **21**, relay GPIO **2** (active HIGH). Classic ESP32 and Nano ESP32 are not supported. |
| **Which scales work?** | Designed first for **Bookoo** Themis Mini/Ultra. Also Acaia, Felicita Arc, AtomHeart Eclair via [AcaiaArduinoBLE](../libraries/AcaiaArduinoBLE/README.md#scale-compatibility). See [Scales](settings/scales.md). |
| **How do I choose among several scales?** | **Always use this scale** (on by default) plus **Preferred scale**. See [Scales](settings/scales.md). |
| **Does it work on machines other than the Micra?** | **No**, not officially. This firmware is for the Linea Micra (paddle on GPIO, CN9 through an isolated relay). |
| **Do I need a custom ShotStopper PCB?** | No. The development board in [Hardware](HARDWARE.md) is an ESP32-S3 1-channel relay module. BOM and Micra wiring are still TODO. |
| **What does the blue LED mean?** | GPIO 1 HIGH while a BLE scale is connected. Toggle in [Alerts](alerts.md). Not part of CN9 decisions. |
| **My wiring uses different GPIOs. How do I change them?** | Not from the Web UI. Edit `shotStopper/ShotStopperHardware.h` and rebuild. See [Hardware](HARDWARE.md). Defaults match the relay board: paddle 21, relay 2 **active HIGH**, paddle **active LOW**. |
| **Why beeps after the shot already ended?** | [Paddle-off reminder](alerts.md): the paddle is still ON and CN9 is already open. Default interval 10 s, limit 15 min. |

## Safety and diagnostics

| Question | Answer |
| --- | --- |
| **What happens after a panic or watchdog reset?** | Boot forces CN9 **open**, then starts normally. No paddle recovery gesture is required. Active hardware/feedback/timer faults can still lock out new closes. |
| **How do I see why a shot ended?** | Shot history: `cut_type` (`auto`, `manual`, `limit`) and `stop_detail` (`normal_target`, `extended_max_weight`, `extended_min_time`, `slow_max_time`, `slow_min_weight`, `auto_to_manual`, …). |
| **The board LED does not light with the scale connected.** | Check **Blue LED while scale connected** (on by default) and that BLE shows connected. |
| **Is the ESP32 relay enough as a safety guarantee?** | **No.** Watchdogs and the 60 s cap reduce lockups, but a welded contact needs a second isolated barrier (K2). See [Hardware](HARDWARE.md). |
| **After an ESP32 core bump I see `ble=fail` but the Web UI works.** | Use the IDF build in this repo (Arduino-ESP32 **3.3.11**). See [Build environment](BUILD.md). |
| **During a shot the scale drops or the Web UI stutters.** | The firmware favors Bluetooth during BBW. Avoid SoftAP and Wi-Fi scans mid-shot when you can. |

## Where to change it

| What you noticed | Where to look |
| --- | --- |
| Shot time limit (≤ 60 s) | [Brew by weight](features/brew-by-weight.md) — Max BBW time |
| Cut after the scale was lost | [A→M time guard](features/auto-to-manual.md) |
| A few grams over target, too fast | [Fast extraction guard](features/fast-extraction-guard.md); pulses in [Alerts](alerts.md) |
| Under target, too slow | [Slow extraction guard](features/slow-extraction-guard.md) |
| Did not stop at target at the start | [Cup protection](features/cup-protection.md) — BBW protection |
| Late cup | [Tare](settings/tare.md), [Cup](settings/cup.md) |
| No weight stop | [Brew by weight](features/brew-by-weight.md) OFF |
| First BBW shot blocked without a scale | [No-scale BBW](settings/no-scale-bbw.md) |

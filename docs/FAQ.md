# Frequently Asked Questions (FAQ)

Odd automatic behavior, lost access, and hardware questions. Defaults are a
fresh flash or a [factory reset](settings/factory-reset.md).

Feature and setting details (ranges, tables) live in the pages linked from
the [README](../README.md). This FAQ answers “why did that happen?”

## Shot and scale

| Question | Answer |
| --- | --- |
| **I cannot brew longer than 60 s. Why?** | A firmware **hard cap of 60 s** applies to every path that closes the machine circuit. The Web UI cannot raise it. Separate from **Max BBW time**. See [Brew by weight](features/brew-by-weight.md). |
| **My shot stops around 50 s (or another value under 60 s).** | Check **Max BBW time** (default **50 s**) on an **automatic BBW** shot. Timer-only / no-scale shots ignore it and run until paddle/switch or the 60 s cap. [A→M time guard](features/auto-to-manual.md) can also cut if the scale dropped. [Slow extraction guard](features/slow-extraction-guard.md) may decide at **44 s**; that is not a replacement for Max BBW time. |
| **The shot ended off-target and the scale was not stopping the flow.** | Often [A→M time guard](features/auto-to-manual.md) (on by default). If the scale is lost mid-shot, weight stop pauses, reconnect continues, and machine circuit can still open on a deadline from shot start. Status shows `Off` / `Idle` / `Armed` / `A→M · …s`. |
| **The shot finished 2–4 g over target.** | Expected when [Fast extraction guard](features/fast-extraction-guard.md) is on and the target arrived **too soon**. The shot extends toward max recovery weight or min BBW brew time. Turn the guard off if you do not want that. Pulses: [Alerts](alerts.md). |
| **The shot finished under target (for example 34 g) when it was slow.** | Expected when [Slow extraction guard](features/slow-extraction-guard.md) is on and the target was not reached by max BBW brew time. BBW still wins if you hit target on time. |
| **The scale hit target but the shot did not stop.** | [Cup protection](features/cup-protection.md) / **BBW protection** (default **12 s**) blocks automatic weight stop at the start so a finger, late cup, or noise cannot cut. First drops can still beep. |
| **Can I put the cup down after I start?** | **Yes**, if **Automatic retare** is on (default). Place the cup in the **retare window** (default **4 s**). Any stable load at or above **Minimum cup weight** (default **10 g** — cardboard ~12–20 g or ceramic 150 g) is a cup, not first drop, and can retare. A finger tap is also not first drop. Then **Post-tare grace** (default **2 s**) lets the scale settle before weight is used to stop. See [Tare and retare](features/tare-retare.md), [Tare](settings/tare.md), and [Cup](settings/cup.md). |
| **Why does it not stop exactly on the target grams?** | A **learned stop offset** (default 1.5 g, max 5 g) compensates for drip after machine circuit opens. History shows `cut_type` and `stop_detail`, not a “prediction” type. See [Brew by weight](features/brew-by-weight.md). |
| **The scale disconnected mid-shot. What happens?** | Weight stop pauses; reconnect continues for the whole cycle. If three coherent samples return, BBW resumes. If [A→M](features/auto-to-manual.md) is on, machine circuit may still open on the deadline. If A→M is off, the shot runs until paddle OFF or the 60 s wall. |
| **I turned Brew by weight off. Why no weight stop?** | Tare and the timer remain. Weight stop, retare, BBW protection, Max BBW time, and offset learning do not. Cut is paddle/switch, the 60 s firmware cap, or remote **Stop**. Fast, Slow, A→M, and No-scale BBW become read-only on Home. |
| **Home Brew by weight is OFF but Settings is ON.** | Both can be right. **Home** is the live session (Manual) and is what the next shot uses. **Settings → Brew** is the saved recipe and does not flip off when you turn Home off. Turn Home ON to resume weight stop without changing the recipe. |
| **BBW is on and the scale is off. Can I prevent all use?** | [No-scale BBW](settings/no-scale-bbw.md) has **Allow manual brewing**, **Warn once, then allow** (default), and **Require a scale**. Require a scale blocks shots and rinses without a scale. If the scale is unavailable in an emergency, start with the activator OFF and switch it ON and OFF three times within two seconds. The connection melody confirms that manual brewing is temporarily available; protection returns after the configured cooldown. |

## Network and access

| Question | Answer |
| --- | --- |
| **What are the default credentials?** | AP **`AdvancedShotStopperAP`** / device password **`ineedacoffee`**. Web UI in AP: **`http://192.168.4.1`**. See [First connection](../README.md#first-connection) and [AP](settings/ap.md). |
| **I cannot find the device Wi-Fi after first boot.** | With no home Wi-Fi saved, SoftAP is up at boot for up to **3 minutes** idle (resets while a client is associated). With saved credentials, STA is tried first; SoftAP appears only if STA fails for ~25 s at boot, with the same idle shutdown. After SoftAP idle-stops or a successful STA join, SoftAP does **not** auto-raise again — use USB `AP_START` or reboot. See [Wi-Fi](settings/wifi.md) and [AP](settings/ap.md). |
| **How do I open the UI after I saved home Wi-Fi?** | Stay on the wait overlay: it polls this address and reloads when the controller returns, which confirms the new network. After a static-IP change, open `http://<new-ip>` if this page does not come back. Serial logs at **115200** also print the address. If the new settings are unreachable, Wi-Fi reverts to the previous network in ~3 minutes and the overlay reconnects there. |
| **Can I change the device password?** | **Admin** (unlock with the current device password) **→ Device password** (new + confirm). USB: `SET_DEVICE_PASSWORD` / `RESET_DEVICE_PASSWORD`. See [USB serial CLI](SERIAL_CLI.md). |
| **I lost Wi-Fi or the device password. How do I recover over USB?** | Hold **GPIO 4 to GND** at reset so CDC enumerates ([Hardware](HARDWARE.md)), 115200 baud. A build flashed with `-DSHOT_STOPPER_ENABLE_JTAG=1` enumerates without the jumper. `HELLO` replies `how are you` even if serial debug is off. `RESET_DEVICE_PASSWORD`, `CLEAR_WIFI`, or `FACTORY_RESET`. Destructive commands need paddle OFF. |
| **No Web UI, Wi-Fi, BLE, or USB. How do I recover?** | [Emergency recovery with the paddle](EMERGENCY_RECOVERY.md). Power on with paddle ON: three cycles restore access; five do a factory reset. Momentary-switch builds use the same gesture: hold the button at power-on. |
| **Why can't I start a shot or rinse from the Web UI or remotely?** | For safety, the project deliberately does not allow remotely starting a shot or rinse. An espresso machine operates with high-temperature water and pressurized steam, so it should not be activated remotely, unattended, or without a person present. Monitoring remains available; the physical paddle/switch is how a shot or rinse is started. |
| **How do I close Admin unlock?** | **Lock** in the header or at the top of Admin. Unlock stays while the Admin page is open, or for 15 minutes after Start/Stop, rinse, Wi-Fi, or OTA. Leaving Home polling does not renew it. |
| **The Web UI looks like garbage in `curl`.** | HTML is gzip. Browsers decode it. Use `curl --compressed http://<ip>/`. |
| **Serial shows `FT-PSK present but FT disabled, falling back to WPA2-PSK`.** | Harmless WPA2 fallback: the router advertises 802.11r and the ESP32 does not use it. Current firmware silences that IDF `wifi` warning. Not a failed join. |
| **Wi-Fi or the Web UI is flaky.** | **Wi-Fi sleep** may be on (Admin → Wi-Fi; on by default). STA can use modem sleep while associated, including with a scale linked, which can lag or drop the UI. Turn the checkbox off. See [Wi-Fi](settings/wifi.md). If the UI stutters while waiting for a scale, try Admin → Bluetooth → Scan intensity **Light**. |
| **Why do webhooks support HTTP but not HTTPS?** | The target ESP32-S3 board has limited internal resources. HTTPS is technically possible, but creating a TLS client during a shot adds RAM, CPU, and radio work that could delay or complicate the Bluetooth scale connection. Webhooks are therefore HTTP-only, asynchronous, and intended for a trusted local network. |
| **Why can I configure only one webhook?** | Even one webhook request is a meaningful workload for the ESP32 while it must keep a reliable Bluetooth connection to the scale. Multiple destinations would add Wi-Fi and CPU activity, increasing the risk of BLE packet loss or connection disruption—especially during a shot. Send the event to one local endpoint, such as Home Assistant, and fan it out there if needed. |
| **The scale takes several seconds to connect after power-on.** | Admin → Bluetooth → **Scan intensity**. Default is **Normal** (50%). Use **Aggressive** if discovery still exceeds ~2 s; **Light** if the Web UI stutters while waiting. |

## Hardware and compatibility

| Question | Answer |
| --- | --- |
| **Which boards can I use?** | ESP32-S3 with PSRAM only: **n16r8** (development board in [Hardware](HARDWARE.md)) or **n8r4**. Default pins: activator GPIO **21**, relay GPIO **2** (active HIGH). Classic ESP32 and Nano ESP32 are not supported. |
| **Which scales work?** | Designed first for **Bookoo** Themis Mini/Ultra. Also Acaia, Felicita Arc, AtomHeart Eclair, Decent, DiFluid Microbalance, MyScale, Varia AKU, Eureka Precisa (named GAP), and WeighMyBru via [EspressoScaleBLE](../libraries/EspressoScaleBLE/README.md#scale-compatibility). Timemore DUO/Dot, Acaia Umbra, and nameless Eureka units are not supported. See [Scales](settings/scales.md). |
| **How do I choose among several scales?** | **Always use this scale** (on by default) plus **Preferred scale**. See [Scales](settings/scales.md). |
| **Does it work on machines other than the Micra?** | It **started** on the Linea Micra and is not a certified kit for every machine. The same isolated-relay contract has three compile-time builds: paddle/latch, momentary, and momentary+reed — see [Machine types](../README.md#machine-types). On the Micra the intercepted brew-switch connector is labelled CN9; user-facing copy always says **machine circuit**. You still design and validate your own wiring. |
| **Do I need a custom ShotStopper PCB?** | No. The development board in [Hardware](HARDWARE.md) is an ESP32-S3 1-channel relay module. BOM and Micra wiring are still TODO. |
| **What does the blue LED mean?** | GPIO 1 HIGH while a BLE scale is connected. Toggle in [Alerts](alerts.md). Not part of machine-circuit decisions. |
| **My wiring uses different GPIOs. How do I change them?** | Not from the Web UI. Edit `shotStopper/ShotStopperHardware.h` and rebuild. See [Hardware](HARDWARE.md). Defaults match the relay board: paddle 21, relay 2 **active HIGH**, paddle **active LOW**. |
| **Why beeps after the shot already ended?** | [Paddle-off reminder](alerts.md): the paddle is still ON and machine circuit is already open. Default interval 10 s, limit 15 min. |

## Safety and diagnostics

| Question | Answer |
| --- | --- |
| **What happens after a panic or watchdog reset?** | Boot forces machine circuit **open**, then starts normally. No paddle recovery gesture is required. Active hardware/feedback/timer faults can still lock out new closes. |
| **How do I see why a shot ended?** | [Shot history](features/shot-history.md): `cut_type` (`auto`, `manual`, `limit`) and `stop_detail` (`normal_target`, `paddle`, `web_stop`, `wall_limit`, `hard_limit`, `extended_max_weight`, `cup_removed`, …). `other` is only for unknown/legacy rows. |
| **The board LED does not light with the scale connected.** | Check **Blue LED while scale connected** (on by default) and that BLE shows connected. |
| **Is the ESP32 relay enough as a safety guarantee?** | **No.** Watchdogs and the 60 s cap reduce lockups, but a welded contact needs a second isolated barrier (K2). See [Hardware](HARDWARE.md). |
| **After an ESP32 core bump I see `ble=fail` but the Web UI works.** | Use the IDF build in this repo (Arduino-ESP32 **3.3.11**). See [Build environment](BUILD.md). |
| **During a shot the scale drops or the Web UI stutters.** | The firmware always prefers Bluetooth on the shared 2.4 GHz radio. Avoid SoftAP and Wi-Fi scans mid-shot when you can. |

## Where to change it

| What you noticed | Where to look |
| --- | --- |
| Shot time limit (≤ 60 s) | Automatic BBW: [Brew by weight](features/brew-by-weight.md) — Max BBW time. Manual / BBW off: 60 s firmware cap only. |
| Cut after the scale was lost | [A→M time guard](features/auto-to-manual.md) |
| A few grams over target, too fast | [Fast extraction guard](features/fast-extraction-guard.md); pulses in [Alerts](alerts.md) |
| Under target, too slow | [Slow extraction guard](features/slow-extraction-guard.md) |
| Did not stop at target at the start | [Cup protection](features/cup-protection.md) — BBW protection |
| Late cup | [Tare and retare](features/tare-retare.md), [Tare](settings/tare.md), [Cup](settings/cup.md) |
| No weight stop | [Brew by weight](features/brew-by-weight.md) OFF |
| First BBW shot blocked without a scale | [No-scale BBW](settings/no-scale-bbw.md) |
| Wi-Fi/UI lag with sleep on | Admin → Wi-Fi — [Wi-Fi sleep](settings/wifi.md); scan duty is Admin → Bluetooth |
| Slow scale discovery after power-on | Admin → Bluetooth — Scan intensity |

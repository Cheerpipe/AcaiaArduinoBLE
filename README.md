# Micra Shot Stopper

ESP32-S3 firmware that controls the CN9 paddle circuit of a La Marzocco Micra
through an isolated relay contact and uses a Bluetooth Low Energy scale to
automate an extraction by weight. It is **dedicated to the Micra** and was
**designed primarily for a Bookoo scale** (Themis Mini/Ultra). Other compatible
BLE scales still work through the vendored AcaiaArduinoBLE library.

The Micra physical paddle **does not connect to CN9**: it connects only between
the configured ESP32-S3 GPIO and GND. The relay COM/NO contact is the controller's
only connection to CN9. This lets the software read the paddle and control CN9
independently.

> **Safety notice:** verify continuity, isolation, module polarity, and that the
> relay remains open during startup, reset, and power loss. Complete the entire
> [manual test plan](docs/MANUAL_TEST_PLAN.md) before connecting the machine.
> This project cannot make an unsuitable relay module or unsafe wiring safe.
> See the [Disclaimer](#disclaimer) — use at your own risk.

## Origin and evolution

This project began from
[tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) — the
original ESP32 firmware and library for stopping an extraction by weight over
BLE. What started as small Micra-specific changes grew into a **new product
concept and an almost complete rewrite**. Micra Shot Stopper is now the main
application; the derived library remains in this repository as a local
dependency.

### Compared to the original firmware

| | Original AcaiaArduinoBLE stopper | Micra Shot Stopper |
| --- | --- | --- |
| **Scope** | Generic, intended to work on several machines | Dedicated to the Micra and its independent paddle; designed primarily for Bookoo |
| **Architecture** | Simple, mostly single-threaded | FreeRTOS tasks, queues, and isolation between control, BLE, and network |
| **Features** | Minimal brew-by-weight stop | Rich workflow: retare, BBW protection, rinse, shot history, Web UI, diagnostics, safety layers |
| **Config** | Bluetooth app; basic settings | Web UI — feature-rich, full control, presets, and shot history |
| **Paddle machines** | Assumes you can work within the original machine constraints | Reads the physical paddle on GPIO and controls CN9 independently — no need to “fight” the paddle wiring |
| **Resilience** | Straightforward happy path | Explicit state machines, watchdogs, transactional CN9 close, stream validation, recovery paths |

The original firmware proved that BLE scale stop was possible; this project
**removed the original’s limitations on paddle-equipped machines** and invested
heavily in robustness: real thread boundaries, CN9 safety defenses, fault
handling, and persistence that survives resets and misconfiguration.

### What was added along the way

Examples that did not exist (or barely existed) in the original stopper sketch:

- **Intelligent retare** and BBW protection so late cup placement does
  not break the shot.
- **Embedded Web UI** with Wi-Fi, diagnostics, configuration, and optional
  remote control — no extra display or buttons on the machine.
- **Shot history** (duration, weight, flow, first drop, cut type, extraction guard
  telemetry) with export.
- **Fast extraction guard** — optional extension when target weight is reached
  too quickly (minimum brew time + max recovery weight); on by default.
- **Slow extraction guard** — optional floor when the shot is still under
  target at max brew time (max brew time + min recovery weight); on by default.
- **Auto-to-manual time guard** — on by default; caps CN9 time if an automatic
  shot loses the scale mid-brew (Auto trend or Manual limit); reconnect stays
  preferred for the whole cycle.
- **Advanced workflow settings** (rinse, Max BBW time, reminders, scale options).
- **Safety and observability** — supervisor, task watchdog, optional external
  K2/feedback, structured debug log, hardware monitor.
- **OTA over Wi-Fi** — planned; see [Not yet implemented](#not-yet-implemented).

The codebase is intentionally **more complex to implement and configure** so
that day-to-day use can stay **simple**.

## Design philosophy

The real goal is to use **brew by weight without noticing the DIY controller is
there**:

- Put the cup **before** the shot or **right when** the shot starts.
- Flip the paddle, brew, walk away.
- No extra screen on the espresso bar, no accessory buttons, no ritual double
  tare, no “did the stopper accept my cup?” — the firmware handles timing,
  retare, BBW protection, stop, and reminders in the background.

Complexity lives in firmware parameters, state machines, and safety — not in
the barista workflow. Defaults and automatic behaviors (retare, BBW protection,
offset learning, paddle-return beeps) exist so most users never touch the Web
UI after initial setup. If something feels unexpected, see the
[FAQ (Spanish)](docs/FAQ.md). The UI and API are there for tuning and diagnosis, not
for operating every shot.

In short: **hard to build, easy to live with.**

## Paddle modes

The firmware offers **three paddle operating modes**. Choose them in the Web UI
under **Settings → Machine and scale → Paddle** (`paddleMode`; machine-level,
not per-preset). Selector order is **Auto**, **Natural**, **Original**. Default
is **Natural**.

> **This is the main day-to-day choice.** All modes still share quick rinse,
> CN9 safety limits, and physical-paddle priority. They differ in whether
> releasing the paddle mid-shot ends the brew or leaves brew-by-weight in
> control.

| | **Auto** | **Natural** (default) | **Original** |
| --- | --- | --- | --- |
| **Feel** | Automatic BBW finish; paddle ON/OFF switches subtype | Paddle = normal brew switch | Legacy [Tater Mazer Shot Stopper](https://github.com/tatemazer/AcaiaArduinoBLE) start gesture |
| **ON** | Starts the extraction (auto-natural) | Starts the extraction | Starts the extraction |
| **OFF after rinse window** | **Keeps CN9 closed** (auto-original) on automatic BBW+scale shots | Ends the shot and opens CN9 | **Keeps CN9 closed** on automatic BBW+scale shots; stopper finishes by weight |
| **Early manual cut** | No paddle cut on BBW+scale (Stop remote / walls still apply). ON↔OFF only switches subtype | Move paddle **OFF** | Move paddle **ON** (promotes that shot to Natural), then **OFF** to cut |
| **When it applies** | Subtype switch only for **automatic brew-by-weight with a usable scale**; no-scale and timer-only behave like Natural | Always | Special hold/auto-stop rules only for **automatic brew-by-weight with a usable scale**; no-scale and timer-only behave like Natural |

### Natural

The paddle works **naturally**, like a normal Micra brew switch:

1. Move the paddle **ON** → CN9 closes and the shot starts.
2. Leave it **ON** → the stopper finishes automatically by weight (when BBW and
   the scale are available), or keep holding for a manual/no-scale cycle.
3. Move it **OFF** at any time after the rinse gesture window → the shot ends
   and CN9 opens immediately.

Short ON→OFF within the **quick rinse gesture** still demotes to a rinse.

### Original

Legacy Shot Stopper workflow for automatic brew-by-weight:

1. Move the paddle **ON** to start.
2. After about **2–3 s** (past the rinse gesture), move it **OFF**. The stopper
   **keeps the extraction running** and ends it automatically by weight.
3. While the paddle stays **ON** during that automatic BBW shot, weight stop is
   held off and the configured Max BBW time does not cut yet; the absolute
   **60 s** CN9 hard cap still applies.
4. To stop early: move the paddle **ON** during the shot (that shot is promoted
   to Natural semantics), then **OFF** when you want to cut.
5. A short ON→OFF inside the rinse gesture is still a **quick rinse**.

**Particularities of Original:**

- Applies only while the cycle is automatic BBW with a scale. Without a usable
  scale, or with Brew by weight off (timer-only), paddle OFF ends the shot like
  Natural.
- Do **not** press the scale pan with a finger to “cancel” — BBW protection and
  retare may ignore accidental weight.
- After an automatic weight stop with the paddle still ON, the usual
  paddle-return reminder still asks you to return the paddle to OFF.

### Auto

Automatic brew-by-weight finish **regardless of whether the paddle stays ON or
OFF**. In one Auto shot the paddle only switches subtype (the saved mode stays
Auto):

1. Move the paddle **ON** to start. While it stays ON the shot is
   **auto-natural**: Natural rules (weight / A→M / Max BBW time still cut with
   the paddle ON; paddle-return reminder after CN9 opens), except OFF after the
   rinse window does **not** end the shot.
2. Move it **OFF** after the rinse gesture → **auto-original**: same as Original
   with the paddle released (CN9 stays closed until automatic stop).
3. Move it **ON** again → back to **auto-natural**. OFF again → auto-original.
   You can switch back and forth in the same shot.
4. A short ON→OFF inside the rinse gesture is still a **quick rinse**.

**Particularities of Auto:**

- Subtype switching applies only while the cycle is automatic BBW with a scale.
  Without a usable scale, or with Brew by weight off (timer-only), paddle OFF
  ends the shot like Natural.
- Unlike Original, Auto does **not** hold off weight stop while the paddle is
  ON, and ON does not promote to a one-way Natural cut on the next OFF.
- After an automatic weight stop with the paddle still ON, the usual
  paddle-return reminder still asks you to return the paddle to OFF.

## Main features

### Micra paddle and CN9 control

- Independent read of the **physical paddle** (GPIO ↔ GND) and control of **CN9**
  through an isolated relay COM/NO contact.
- Three configurable **[paddle modes](#paddle-modes)** — **Auto**, **Natural**
  (default), and **Original** (legacy start-gesture BBW).
- Safe startup: CN9 stays open until a stable physical paddle OFF is detected
  (`REQUIRES_OFF` → `READY`).
- Short-gesture **quick rinse** (configurable gesture time and rinse duration),
  manual extraction, and automatic **brew by weight** when a scale is available.
- **Physical paddle has priority** over every remote path. Web commands never
  bypass paddle safety or an active local cycle.
- Configurable **Max BBW time** per cycle (5–60 s; default 50 s) plus a firmware hard cap of
  60 s on every close path.
- Learned **stop offset** (default 1.5 g, capped at 5.0 g) updated from
  post-drip analysis; resettable from the Web UI to a configurable baseline
  (factory default 1.5 g).
- **Polarity:** physical paddle ON = GPIO **LOW**; relay closed (CN9
  energized) = GPIO **LOW**.

### Shot presets

Brew settings live in **named presets** (factory **Single** / **Double**, plus customs). The active preset supplies goal weight, BBW/guards, learned stop offset and its baseline, and A→M samples. Changing preset persists only the active id (no copy-on-select). Manage presets under **Settings → Brew** (dense cards: New · Duplicate · Load · Save · Delete; rename on the card). **New** seeds firmware Double defaults (not the saved Double in NVS). **Home → Quick Settings** has **Brew by weight** (session Manual), plus **No-scale BBW** (machine), **Fast extraction guard**, **Slow extraction guard**, **A→M time guard**, and the machine-level **Alerts** master switch. Guard switches persist immediately (machine config for No-scale; active preset for the other three) and become read-only when Brew by weight is off; Alerts remains editable independently. Machine/scale, alerts, Wi‑Fi, and password stay outside the recipe.

### Brew and weight settings

All workflow parameters below are editable from the Web UI **Settings**
panel and persisted in **NVS** (`Preferences`, dual slots `settingsA` /
`settingsB`, config schema **v1**). Defaults are shown in parentheses.

| Setting | What it does |
| --- | --- |
| **Target (g)** | Goal weight for brew by weight (10–200 g; default 36 g). |
| **Max BBW time (s)** | Maximum brew-by-weight cycle time (5–60 s; default 50 s). |
| **Baseline offset (g)** | Seed for **Reset learned stop offset to baseline** (0–5 g; factory default 1.5 g). Save before reset. |
| **Fast extraction guard** | **On by default**. See [Fast extraction guard](#fast-extraction-guard). |
| **Slow extraction guard** | **On by default**. See [Slow extraction guard](#slow-extraction-guard). |
| **Auto-to-manual time guard** | **On by default**. See [Auto-to-manual time guard](#auto-to-manual-time-guard). |
| **Automatic tare** | Send an initial tare when an automatic shot starts (default ON). |
| **Post-tare grace (s)** | Machine-level (**Settings → Machine and scale → Tare**). After a tare (start or late-cup retare), wait this long for the scale to report ~0 g before using weight for first drop and stop. Default **2 s**; 0.5–10 s. Inactive when Automatic tare is off. |
| **Brew by weight** | **On by default**. Stop the shot by scale weight. Off keeps tare/timer but disables weight stop, BBW protection, automatic retare, and offset learning (same as the former Timer only setting). |
| **Bookoo combined command** | Use the scale’s combined tare + start-timer command (requires auto tare; default ON). |
| **Mute scale in Buzzer only** | Bookoo/generic: when enabled (default ON), send silence (volume 0) on connect/reconnect, when Output channel is saved as Buzzer only, and when this option is turned on. Applies only in **Buzzer only**. |
| **Scale volume** | Bookoo/generic: on connect/reconnect, set scale volume 1–5 (default 4) or Disabled. Applies only in **Scale only** and **Scale priority**. |
| **AtomHeart Eclair** | Informational brand section. Eclair uses the normal tare/timer commands and exposes no configurable volume, beep, mode, combined tare-and-start command, or documented command sound. In **Buzzer only** and **Scale priority**, its alerts use the local buzzer; **Scale only** omits unsupported sounds. |
| **Automatic retare** | Allow one late-cup retare during the retare window (default ON). |
| **Retare window (s)** | Time after shot start to detect and retare a late-placed cup (default 4 s). |
| **Minimum cup weight (g)** | Stable load threshold that qualifies as a cup for retare (default 10 g). |
| **Retare stability** | Samples (default 3), tolerance (default 2.0 g), max sample gap (default 0.5 s), and min stable time (default 0.3 s) required before retare fires. |
| **BBW protection (s)** | **Pre-arm / accidental-weight protection window** at shot start for automatic BBW: inhibits automatic weight stop until the timeout expires (default 12 s; minimum retare window + 3 s). Runs in parallel with retare and first-drop detection; first drops do not end this window. Skipped when Brew by weight is off. |
| **Avoid BBW shot without scale** | **On by default**. Machine-level (Settings → Machine and scale, first group; not per-preset). With Brew by weight on and no usable scale, a paddle ON longer than the **quick rinse gesture** **does not close CN9**. A shorter ON→OFF is a rinse: CN9 closes and the guard goes **Idle**. The no-scale triple beep still plays on paddle ON (brew or rinse) whenever BBW is on and the scale is missing. The next start after a blocked shot or an Armed rinse runs as a manual no-scale shot. Re-arms on boot, when the scale becomes available, or after **Last shot cooldown**. |
| **Paddle mode** | Machine-level (**Settings → Machine and scale → Paddle**). **Auto**, **Natural** (default), or **Original**. See [Paddle modes](#paddle-modes). |
| **Always use this scale** | **On by default** (`scaleMacCacheMode=full`). Name-scan for compatible scales; when a preferred MAC is set, only that MAC is connected; other compatible advertisements are stored in history (up to 8) without connecting. Off connects the first compatible scale and does not lock preferred. |
| **Preferred scale** | Dropdown of BLE-seen scales (history). Select which MAC is preferred, or **Clear preferred** (30 s discovery pause; history kept). |
| **Drip delay** | Machine-level (**Settings → Machine and scale → Scales**). Wait after a shot ends before capturing the final post-drip weight used by Last Shot, history, offset learning, and eligible A→M samples (default 3.0 s; 0–10 s). A value of 0 finalizes on the next control loop without an intentional post-drip window. |
| **Last shot cooldown (min)** | Time after a blocked start, an Armed rinse that consumed the guard, or a finished (non-rinse) shot before the no-scale guard re-arms (default 60 min; 5–240). Boot and scale reconnect re-arm immediately. |
| **Quick rinse gesture (s)** | Maximum paddle ON time that still counts as a quick rinse when released (default 1 s). |
| **Quick rinse duration (s)** | How long CN9 stays closed after a quick rinse starts (default 4 s). |
| **Timezone offset (min)** | Wall-clock offset for shot history labels (default UTC+0). |
| **NTP server** | Preset (default **pool**) or custom hostname for time sync. |

Additional fixed protections (not separately configurable):

- **Direct threshold stop:** two fresh samples at `target − learned offset`
  after BBW protection ends.
- **Predictive stop:** linear regression over recent accepted samples can stop
  earlier than the direct threshold.
- **Scale loss handling:** BLE disconnect or stale stream suspends weight
  control; recovery requires three coherent samples on the current connection
  generation. Paddle OFF and time limits remain authoritative.

### Alerts and reminders

Machine-level (not per-preset). Sounds are **event-first**: tare/start/stop,
first drop, paddle reminder, completion extra, and the triple alerts are
routed by **Output channel** when a local buzzer is compiled in.

| Setting | What it does |
| --- | --- |
| **Sound alerts** | Master alert switch (default ON). Off silences operational alerts on both buzzer and scale while preserving the individual alert settings. |
| **Output channel** | Shown only with `SHOT_STOPPER_ENABLE_BUZZER`. Default is **Buzzer only** when a local buzzer is compiled in, and **Scale only** without buzzer support. **Buzzer only**: all alert sound via the local buzzer (for a silent scale). **Scale only**: scale path only; scale-incapable triples are muted. **Scale priority**: scale when connected/able, else buzzer; never both for one event. In Buzzer only (and Scale priority when the scale is not usable), tare/start/stop sounds follow CN9/paddle/retare immediately and do not wait for the BLE round-trip. |
| **Beep when coffee starts** | One beep on first coffee drops during an automatic shot (default ON; ignored when Brew by weight is off). |
| **Paddle-off reminder** | Repeat beeps while the **physical paddle stays ON** and **CN9 is open** (default ON). |
| **Paddle reminder interval (s)** | Time between reminder beeps (5–60 s; default 10 s). |
| **Paddle reminder limit (min)** | Stop beeping after this duration even if the paddle remains ON (1–60 min; default 15 min). |
| **Scale lost** | Echo inverted on the local buzzer when the scale disconnects (idle or during a shot). Shown with buzzer support; disabled when Output channel is Scale only. |
| **ATM / manual-no-scale** | Triple beeps on the local buzzer when A→M ends or when BBW needs a scale that is missing. Shown with buzzer support; disabled when Output channel is Scale only. |
| **Scale connected** | Distinctive echo when a scale connects or reconnects. Buzzer only or Scale priority (always local buzzer; default ON). |
| **Blue LED while scale connected** | Onboard GPIO1 LED HIGH while a scale is BLE-connected (default ON). Independent of Sound alerts. |
| **Extended shot pulse** | Local-buzzer pulses while Fast extraction guard keeps the shot running past the normal BBW cut. Dropdown: Disabled, Slow, Medium, **Fast (default)**, Rapid. Shown with buzzer support; disabled when Output channel is Scale only. |
| **Slow extended pulse** | Same rate dropdown for Slow extraction guard (past max brew time until min recovery weight). Default Fast. |

Shot completion still adds one extra beep after stop (not configurable). Without
buzzer support, Output channel and the triple checkboxes are hidden.

### Shot history

- Up to **120** completed extractions stored in NVS (shot log schema **v6**;
  writes a compact blob of only used records;
  minimum duration 10 s; rinses and very short gestures are excluded).
- Each record includes: local time (when NTP synced), duration, goal weight,
  actual weight, `actual_weight_source` (`post_drip` / `last_known` / `none`),
  error and error %, learned offset used, average flow (g/s), first-drop time,
  shot type (`auto`, `timer_only`, `manual`), **cut type**
  (`auto`, `manual`, `limit` — how CN9 opened), **stop detail**
  (`normal_target`, `extended_max_weight`, `extended_min_time`,
  `auto_to_manual`, `other`), and when the fast extraction guard was active:
  whether the shot was extended, `targetReachedEarlyS`, and the max recovery
  weight / minimum brew time that applied.
- Web UI table with **CSV export**, authenticated **clear all**, and
  **per-shot delete**.
- Timezone offset and NTP server (preset or custom) configure wall-clock labels;
  manual **Sync now** when signed in.

### Web UI, Wi-Fi, and API

See [Factory credentials (first use)](#factory-credentials-first-use) for AP
name, default WPA2 password, and step-by-step first connection.

- Fully **embedded Web UI** SPA with routes (`/`, `/presets`, `/settings`,
  `/history`, `/admin`, `/debug`, `/log`): same-origin assets only (no CDN). Authoring
  budget HTML ≤ **40 KiB**, JS ≤ **64 KiB**, combined HTML+JS ≤ **100 KiB**;
  gzip HTML ≤ **8 KiB**, gzip JS ≤ **20 KiB**, gzip CSS ≤ **6 KiB**,
  combined ≤ **30 KiB**. Reloads
  revalidate HTML with `ETag` (`Cache-Control: no-cache`) so unchanged firmware
  returns **304**. Shared script is `GET /app.js` and stylesheet is `GET /app.css`
  (gzip, versioned query, long-lived `immutable` cache; `Connection: close`).
  Inactive views do not poll their APIs.
- **STA** when credentials are saved and the home network is reachable;
  **SoftAP** (`MicraShotStopperAP` at `192.168.4.1`) when there are no
  credentials, or only after STA association fails (~15 s) at boot while
  credentials exist. While SoftAP is up and credentials exist, the firmware
  keeps retrying STA in concurrent **AP+STA** mode; once STA connects, SoftAP
  is stopped and HTTP is rebound on the STA IP. After a successful STA join,
  link loss retries STA only (no SoftAP auto-raise; use USB `AP_START` or
  reboot). STA addressing is **DHCP** or **static IP**, with a **3-minute
  confirm-or-revert** window after save.
- SoftAP stays available while unassociated after that boot STA-first window
  (no idle 3-minute shutdown). Once STA connects, HTTP remains on the STA IP
  and SoftAP is not re-raised automatically on later drops. After logout,
  sessions still expire by UI grace / remember-me rules; SoftAP itself is not
  torn down for idle UI.
- The Web UI and REST API are public to any client that can reach the device.
  This includes configuration, controls, Wi-Fi changes, maintenance and
  diagnostics; operational safety gates still apply. In STA mode, use a
  trusted network because there is no Web UI login.
- **Live shot panel:** current/goal weight, progress bar, elapsed time, first
  drop, retare state, shot type, scale protocol, and **last-shot** guard state
  for extraction (`Off` / `On` / extended), A→M (`Off` / `Idle` / `Armed` /
  `A→M · Ns`), and no-scale (`Off` / `Armed` / `Idle`). **Status** shows the
  same three guards as **current** machine state (`Off` / `Idle` / `Armed`,
  plus in-shot detail).
- **REST API** (`/api/v1/…`):
  - Read: `GET /api/v1/status/{page}`, `GET /log`, `GET /shots`
  - Auth: `POST /login`, `POST /logout` (session activity is refreshed by
    authenticated API traffic and optional session headers on `GET /api/v1/status/{page}` /
    `GET /shots`)
  - Config: `POST /config`, `POST /calibration/reset`, `POST /time/sync`,
    `POST /access-point/password` (`currentPassword` and `newPassword`)
  - Network: `POST /network` (`save` / `forget` / `confirm`), `POST /network/scan`,
    `GET /network/scan` (async, max **12** networks, **120 s** timeout;
    cancelable via maintenance lease). `save` accepts `ipMode` (`dhcp`|
    `static`) plus static fields `ip`/`netmask`/`gateway`/`dns1`/`dns2`.
  - Control: `POST /control/paddle`, `/control/rinse`, `/control/stop`,
    `/control/restart`
  - Maintenance: `POST /factory-reset` (confirm `ERASE_ALL_SETTINGS`),
    `POST /shots/clear` (confirm `CLEAR_SHOT_LOG`), `POST /shots/delete`
  - Wi-Fi (STA/AP) and the HTTP server start regardless of paddle position;
    **configuration changes** still use a maintenance lease that requires
    stable paddle OFF and open CN9.
- **Factory reset** erases Wi-Fi, settings, calibration, shot history, and
  restores the AP/UI password to **`Micra1234`**, then restarts.

### Emergency recovery

If Wi-Fi, BLE, Web UI and USB/serial access are unavailable, power the
controller with the physical paddle held **ON**. The local recovery window
keeps CN9 open for 60 seconds. Three complete `OFF→ON` cycles in 5 seconds,
followed by 3 seconds without moving the paddle, restore Wi-Fi/AP/UI access;
five cycles followed by the same confirmation perform a full factory reset.

The factory gesture permanently erases settings, presets, calibration and
shot history. Follow the timings and examples in
[Emergency recovery with the paddle](docs/EMERGENCY_RECOVERY.md).

**Diagnostics** (Admin panel + Log panel + API):

- Paddle state, CN9 relay state, CN9 safety supervisor (state, fault, watchdog,
  external hardware present, recovery required).
- Live **extraction**, **A→M**, and **no-scale** guard state (Off / Idle / Armed
  or in-shot detail). The Shot panel keeps the values from the last cycle.
- Control source (physical vs web), maintenance lease, last command result.
- Scale link: BLE availability, protocol, stream/control state, observed vs
  accepted weight, packet gaps, rejected packets, reconnects, disconnect reason.
- Loop health: max loop gap, free/min heap, dropped debug events.
- Hardware monitor: CPU usage, chip temperature (and peak), RAM total/used/free,
  uptime, and last ESP reset reason.
- NTP/time sync state and configured timezone.
- **Diagnostic log:** bounded ring of **192** events with severity levels
  (Critical / Error / Warning / Info / Debug), categories (including Boot and
  System), uptime or wall-clock timestamps when NTP is synced, level + category
  filters, dropped-event counter, copy, and clear view (client-side only);
  also available via `GET /api/v1/log`. Serial and UI share the same structured
  pipeline (`logEmit`); the Web log ring retains Info by default. USB debug
  print is **off** by default (`serialDebugOutput`); enable it from Debug or
  `SERIAL_DEBUG_ON`. CLI replies on the same port stay on.
- **USB serial CLI** at **115200** baud for recovery, provisioning, and
  network/debug dumps without the Web UI. See
  [USB serial CLI](docs/SERIAL_CLI.md).

**Web paddle and remote control** (opt-in build only):

- **Virtual paddle** toggle (`POST /api/v1/control/paddle`) starts and ends a
  remote cycle when signed in and remote CN9 is enabled. Closing or freezing
  the browser does **not** force-open CN9; use **Stop**, physical paddle, or
  shot limits. Logout / session replace still releases a web-owned lease.
- **Start quick rinse**, **Stop shot** (opens CN9 only), and **Restart
  controller** from the Actions panel.
- **Remote CN9 actuation for new cycles is disabled by default.** Virtual
  paddle and remote quick rinse require compile-time
  `SHOT_STOPPER_ENABLE_REMOTE_CN9=1`. **Stop** always opens CN9 in every
  build (when authenticated). Monitoring, diagnostics, and configuration work
  without the flag. Physical paddle always has priority.

### ShotStopper Companion BLE

The stopper also exposes the Tater Mazer-compatible service `0FFE` as a BLE
peripheral while remaining a BLE central for the scale. It advertises as
`shotStopper` and reports Companion protocol v2. Supported characteristics
cover brew-by-weight settings, target weight, auto tare, timing limits, drip
delay, scale/shot status, Wi-Fi provisioning, IP status, restart, and temporary
SoftAP control. `FF21` is an inert OTA compatibility stub; this firmware does
not start BLE OTA.

Companion BLE is enabled by default and can be configured from **Admin → BLE
Companion** or with `BLE_COMPAT_DISABLE`. Enable/disable changes apply only on
the next boot: when disabled at boot the GATT profile and its characteristics
are never constructed, so their RAM is available to Wi-Fi/httpd while the BLE
central used by the scale remains active. Admin and `BLE_COMPAT_STATUS` expose
configured, active-this-boot, and restart-required states separately. The app
protocol has no pairing, so Wi-Fi passwords are write-only and the service
should be disabled when this compatibility surface is not needed. Setting and
restart writes are rejected unless the stopper is Ready; AP start/stop remains
available independently and never changes the boot preference.

### Scale support

Scale compatibility depends on the
[AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE) library by
**[tatemazer](https://github.com/tatemazer)**. This project vendors a local
copy under `libraries/AcaiaArduinoBLE/` and aims to stay aligned with upstream
releases, new scale types, and protocol fixes as they land in that repository.

- Daily development, defaults, and Bookoo-specific settings (combined tare +
  start-timer, volume, mute in Buzzer only) assume a **Bookoo** Themis Mini or
  Ultra. **Acaia** (legacy and current), **Felicita**, and **AtomHeart Eclair**
  remain supported through the same vendored **AcaiaArduinoBLE** library.
- Dedicated **`scale_worker`** FreeRTOS task isolates BLE polling, connection
  retries, tare/start/stop commands, and beeps from the control loop via
  bounded command and event queues.

### Runtime architecture

Work is split so paddle/CN9 timing never waits on BLE or HTTP:

| Task | Role |
| --- | --- |
| **`loopTask` (Arduino `loop`)** | Paddle debounce, state machine, CN9 arm/open, weight-stop logic, shot logging, web command dispatch, safety supervisor feed, scale-connected GPIO LED. |
| **`scale_worker`** | BLE connection, weight stream, scale commands and beeps. |
| **`network_manager`** | Wi-Fi STA/AP, HTTP server, NTP, NVS writes for config/network. |

`loopTask`, `scale_worker`, and `network_manager` each register a **5 s Task
Watchdog** subscription.

### Safety and indicators

- Generation-based **transactional CN9 close**; three timing defenses (GPTimer
  IRAM handler, `esp_timer`, supervisor deadline).
- Optional **external heartbeat + CN9 feedback** for a second K2 barrier
  (compile-time GPIO pins).
- Redundant RTC relay-command log records whether CN9 was commanded closed;
  every boot forces CN9 open before initialization and starts normally.
- Optional onboard **GPIO LED** (default GPIO 1, active HIGH) while a scale is
  BLE-connected; toggle from **Settings → Alerts**. Diagnostic only, not part
  of CN9 decisions.
- **Host tests** for workflow, persistence, safety, remote policy, BLE library,
  and embedded Web UI asset validation.

## Not yet implemented

The following items are planned but **not present in the current firmware**:

- **OTA (over-the-air firmware updates)** — remote flash of new builds over
  Wi-Fi without USB.
- **Home Assistant integration** — publish status, sensors, and/or controls to
  Home Assistant (MQTT, REST, or native integration).

## Optional hardware: local buzzer

Compile with `-DSHOT_STOPPER_ENABLE_BUZZER=1` (passive piezo) or `=2` (active
buzzer) and wire the buzzer between `SHOT_STOPPER_BUZZER_GPIO` (board default, or
override) and GND: marked **+** to the GPIO, unmarked pin to GND. Default GPIO
is 14.

Identify the part with 3.3 V DC on `+` vs GND: a constant tone is **active**
(`=2`); a click or silence is **passive** (`=1`). Beep on/gap durations are the
same catalog for both; only the drive (PWM vs GPIO HIGH/LOW) differs. Omit the
flag or set `=0` for builds without that hardware. Output channel then defaults
to **Scale only**.

When enabled, Alerts shows **Output channel** (default **Buzzer only**; also
Scale priority / Scale only) plus checkboxes for **Scale lost** (Echo inverted),
ATM-end / manual-without-scale triple beeps, **Scale connected** (Echo; Buzzer
only or Scale priority, default ON), **Extended shot pulse** (Fast extraction),
and **Slow extended pulse** (each Disabled / Slow / Medium / Fast / Rapid;
default Fast). All alert events (including tare/start/stop feedback when the
channel routes to the buzzer) go through that setting. Debug
short/long/double/triple, Slow/Medium/Fast/Rapid (3 s), and
Chime/Swing/Echo/Echo inverted/Morse/Snap buttons play the same catalog as the live alerts.

## Optional hardware: scale-connected GPIO LED

The default pin map drives an active-HIGH LED on **GPIO 1** while a BLE scale
is connected. Turn it off from **Settings → Machine and scale → Alerts → Blue
LED while scale connected**. Override the pin at compile time with
`-DSHOT_STOPPER_SCALE_CONNECTED_LED_GPIO`. See
[Scale-connected LED](#scale-connected-led).

## Repository structure

```text
.
├── VERSION                         # Release version (SemVer)
├── scripts/gen_version.sh          # Build-time version header generator
├── package.json                    # Node tooling (Terser) for Web UI minify
├── scripts/gen_web_ui.js           # Gzip-precompressed Web UI/JS/CSS header generator
├── shotStopper/web/app.js          # Authored Web UI JavaScript (embedded via generator)
├── shotStopper/web/app.css         # Authored Web UI stylesheet (embedded via generator)
├── shotStopper/                    # Main firmware sketch and host tests
│   ├── shotStopper.ino
│   ├── ShotStopperSerialCli.h      # USB serial command parser
│   └── tests/                      # Host tests, web asset + firmware checks
├── libraries/
│   └── AcaiaArduinoBLE/            # Local Arduino library and BLE tests
├── docs/
│   ├── FAQ.md
│   ├── SERIAL_CLI.md
│   ├── PLAN_WEB_SOCKETS_SESSION.md
│   └── MANUAL_TEST_PLAN.md
└── LICENSE
```

`shotStopper/` is the main sketch. `libraries/AcaiaArduinoBLE/` is included
explicitly during compilation with `--library`.

## Functional behavior

At startup, the relay is open. The controller must detect a stable physical
paddle OFF state before entering `READY`. From `READY`, moving the paddle to ON
closes CN9 and starts a brew (or a manual cycle if scale automation is
unavailable). Retare and BBW protection run in parallel from that moment.
Paddle ON/OFF semantics for ending a shot depend on
[paddle mode](#paddle-modes) (**Auto**, **Natural**, or **Original**).

- Releasing the paddle within the rinse gesture time **demotes** the cycle to a
  rinse. CN9 remains closed for the configured rinse duration, and subsequent
  paddle changes are ignored until the rinse ends.
- Holding it ON past the rinse gesture keeps the brew. Automatic by-weight stop
  is available when scale automation was present at start; otherwise the cycle
  stays manual without a scale.
- **Natural:** releasing after the rinse window ends the brew and opens CN9
  immediately (automatic or manual).
- **Original (automatic BBW + scale only):** releasing after the rinse window
  leaves CN9 closed so the stopper can finish by weight; paddle ON again during
  that shot promotes it to Natural, then OFF cuts. Without scale automation or
  with timer-only, release behaves like Natural.
- **Auto (automatic BBW + scale only):** releasing after the rinse window
  leaves CN9 closed (auto-original). Paddle ON is auto-natural (weight stop
  still cuts). ON and OFF switch subtype in the same shot; paddle does not end
  the BBW shot. Without scale automation or with timer-only, release behaves
  like Natural.
- If the scale disconnects or its samples become stale during an automatic
  extraction, weight control is suspended. It recovers only on the current BLE
  generation after three coherent samples. Paddle OFF and timing limits remain
  authoritative throughout.
- After BBW protection ends, two fresh samples at or above
  `goal - offset` stop the extraction directly, independently of regression.
  Prediction remains an earlier stop mechanism. Timer-only mode retains timing
  and tare but disables both mechanisms and learning.
- Every path that closes CN9 is constrained by the configurable operational
  time and an absolute maximum of 60 seconds.

```mermaid
stateDiagram-v2
  [*] --> REQUIRES_OFF
  REQUIRES_OFF --> READY: physical OFF stable
  READY --> BREW: ON with scale automation
  READY --> MANUAL_NO_SCALE: ON without scale automation
  BREW --> RINSE: short gesture (demotion)
  MANUAL_NO_SCALE --> RINSE: short gesture (demotion)
  BREW --> READY: paddle OFF after rinse window
  MANUAL_NO_SCALE --> READY: paddle OFF after rinse window
  RINSE --> READY: complete, paddle OFF
  RINSE --> REQUIRES_OFF: complete, paddle ON
  BREW --> READY: paddle/Web stop
  MANUAL_NO_SCALE --> READY: paddle/Web stop
  BREW --> REQUIRES_OFF: weight, time, or safety stop
```

## Scale stop logic

During an automatic extraction the scale session starts with the cycle. The
cycle enters `BREW` (or `MANUAL_NO_SCALE`) immediately at paddle ON. Retare and
BBW protection run in parallel from shot start and do not delay that entry.

1. **Automatic retare** (if enabled): stable cup load after shot start triggers
   a second tare without restarting the shot timer.
2. **BBW protection / pre-arm** (always active for automatic BBW): runs for the
   configured timeout from shot start. Automatic stop by weight is inhibited
   while BBW protection still blocks. First drops beep and log independently of
   retare; they do not end this window.
3. **Brew by weight**: after both windows end, weight stop is armed
   immediately (same predicted-weight tool as every other recipe weight cut).

Manual stop, CN9 time limits, paddle OFF, and all safety mechanisms remain
active throughout. Timer-only and manual-no-scale cycles skip retare and
BBW protection. Abrupt or implausible samples are visible as observed weight but
cannot enter regression or offset learning.

The status API distinguishes BLE connection, stream freshness, control
authority, observed versus accepted weight, connection generation, packet
gaps, rejected packets, reconnects, and disconnect reason.

## Fast extraction guard

Optional brew-by-weight enhancement, **enabled by default**. It addresses
shots that reach the target weight too quickly — often a sign of channeling or
a grind that is too coarse — where stopping immediately would yield a thin,
under-extracted cup.

When enabled, you still set a mandatory **target weight** (same as today). You
also configure:

| Setting | Default | Role |
| --- | --- | --- |
| **Enable** | ON | Master switch for the extended-shot recovery |
| **Max recovery weight (g)** | 42.5 | Hard ceiling if the shot must be extended |
| **Minimum brew time (s)** | 28 | Minimum extraction time for a normal target stop |

### How it works

1. **Normal stop** — the scale reaches the target at or after the minimum brew
   time → CN9 opens at the target (`normal_target`). BBW does not run before
   that minimum time.
2. **Too fast** — the target is reached *before* the minimum brew time → BBW
   is inhibited and the shot enters **extended** mode until either:
   - **Max recovery weight** (same weight-cut tool as BBW: predicted time plus
     a real-weight backup) → `extended_max_weight`, or
   - **Minimum brew time** is reached *and* the scale still shows at least the
     target weight (stops even if below max recovery) → `extended_min_time`.

Elapsed time is measured from cycle start (CN9 close), consistent with other
timing in the firmware. The learned stop offset applies to both target and max
recovery thresholds.

### Why it is useful

If coffee hits 36 g in ~22 s but you know a good shot needs ~28 s, the guard
lets the extraction continue toward 42.5 g or until the minimum time passes —
similar to manually allowing the shot to reach your next weight checkpoint
without watching the scale.

### Telemetry

The live shot panel shows when the guard is off, on, or **extended** (with the
active stop weight and time remaining). Shot history and CSV export record
`ext_guard`, `ext`, `stop`, `max_rec_g`, `min_brew_s`, `early_s`, plus
`shot_type` and `cut_type`.

## Slow extraction guard

Optional brew-by-weight enhancement, **enabled by default**. It is the inverse
of Fast extraction guard: shots that have **not** reached the BBW target by a
maximum brew time — often a sign of a grind that is too fine or a stalled
extraction — should not wait all the way to the CN9 wall.

When enabled, you still set a mandatory **target weight** (same as today). You
also configure:

| Setting | Default | Role |
| --- | --- | --- |
| **Enable** | ON | Master switch for the slow-shot recovery |
| **Min recovery weight (g)** | 34 (Double) / 34 (Single) | Floor if the shot must be extended past max brew time |
| **Max brew time (s)** | 44 | Latest time to wait for the normal BBW target |

### How it works

1. **Normal stop** — the scale reaches the BBW target at or before the max brew
   time → CN9 opens at the target (`normal_target`). Slow does not fire.
2. **Too slow** — max brew time is reached *without* the BBW target:
   - If the scale is already at or above **min recovery** → cut now
     (`slow_max_time`).
   - If it is still below that floor → the shot enters **extended** mode until
     **min recovery weight** (same weight-cut tool as BBW: predicted time plus
     a real-weight backup) → `slow_min_weight`, or until the CN9 wall.

BBW wins by weight: a shot that hits the target on time is a normal BBW stop.
Fast extended and Slow extended are mutually exclusive; Slow does not interrupt
a shot that Fast already extended (that shot already reached BBW).

Elapsed time is measured from cycle start (CN9 close), consistent with other
timing in the firmware. The learned stop offset applies to the min recovery
threshold (`min recovery − offset`), the same way it applies to Fast’s max
recovery. Prediction is only a tool; the logged reason is always the mechanism
(`slow_max_time` / `slow_min_weight`).

Max brew time is a **decision point**, not a replacement for the CN9 wall. If
the shot is already over the floor at 44 s, it cuts there and the 60 s wall is
rarely used. If it is still under the floor, it may continue toward min
recovery **or** hit the CN9 wall, which remains the hard cap.

When Fast is also ON, max brew time must be greater than min brew time so the
normal BBW window sits between them (28 s–44 s on factory Double).

### Why it is useful

If coffee is only at 20 g at 44 s and you know a drinkable cup needs at least
30 g, the guard either cuts at 44 s (when already over the floor) or lets the
shot continue toward 30 g instead of waiting for 36 g or the 60 s wall.

### Telemetry

The live shot panel shows when the guard is off, idle, on, or **extended**
(with the active stop weight). Shot history and CSV export record `slow_guard`,
`slow_ext`, and `stop` (`slow_max_time` / `slow_min_weight`). Compact history
does not store the min-g / max-s recipe values; those live on the preset.

## Auto-to-manual time guard

Safety layer for automatic brew-by-weight shots that lose the scale mid-
extraction. **Enabled by default.** It does not apply to shots that start
manual (`MANUAL_NO_SCALE`), timer-only mode, or rinse cycles.

### Why it exists

When BLE drops during an automatic shot, weight stop is suspended while the
firmware keeps trying to reconnect for the **entire** cycle. That transition
is easy to miss at the bar: the paddle stays closed, the shot keeps running,
and the only hard stop left may be the CN9 time limit. The guard closes CN9
on a shorter, predictable deadline so a silent auto→manual fallback does not
turn into an over-extracted shot.

There is **no** permanent lockout to manual after a fixed reconnect window.
Reconnect remains preferred; if the scale returns with three coherent samples,
weight control (including Fast or Slow extraction guard, if enabled) resumes and A→M
enforcement clears. A later disconnect in the same cycle reuses the same
absolute deadline from cycle start.

### Configuration

| Setting | Default | Role |
| --- | --- | --- |
| **Enable A→M time guard** | ON | Master switch for the CN9 deadline |
| **Limit mode** | Auto | **Auto** = linear trend of the last five good shot durations; **Manual** = fixed seconds |
| **Manual limit (s)** | 32 | Used when Limit mode is Manual (clamped to 10 s … Max BBW time) |
| **Trend (s)** | ~32 | Read-only; current Auto prediction (always shown) |
| **Baseline duration (s)** | 32 | Seed used by **Reset A→M samples** (five equal values). Factory default for Manual limit |

Deadline = cycle start + limit. Limit is computed once when an automatic brew
is confirmed. Enforcement starts on the first scale-loss suspend after arming;
the live shot panel has a dedicated **A→M time guard** line (`Off` / `Idle` /
`Armed` / `A→M · Ns` while enforced). **Status** shows the current A→M state;
**Shot** keeps the last-shot value after the cycle ends.

### Duration samples

Successful shots continuously feed a ring of five durations (deciseconds),
independent of whether the guard is enabled or which limit mode is selected:

- Total shot duration; error ≤ 10 % vs goal; not Fast/Slow extraction-extended; not rinse;
  not stopped by this guard; post-drip weight available
- Auto and manual shots qualify when weight/error criteria are met
- Fresh devices restore five logical **32 s** samples. **Reset A→M samples to
  baseline** fills all five with the configured **Baseline duration**
  (`POST /api/v1/calibration/reset-guard-samples`; save baseline first)

### Telemetry

- Live panel **A→M time guard** line: `Off` / `Idle` / `Armed` / `A→M · Ns`
  when enforced (separate from Fast and Slow extraction guards)
- History: `stop_detail = auto_to_manual`, `cut_type = limit`;
  `actual_weight_source` may be `last_known` when logged without post-drip weight
- CSV export includes `actual_weight_source`

## Factory credentials (first use)

On a fresh flash (or after **factory reset**), use these defaults to reach the
controller over Wi‑Fi. The password protects only the fallback WPA2 access
point; the Web UI has no login.

| | Value |
| --- | --- |
| **Fallback Wi‑Fi (AP) name** | `MicraShotStopperAP` |
| **Fallback Wi‑Fi (AP) password** | `Micra1234` |
| **Web UI address (AP mode)** | `http://192.168.4.1` |
| **Web UI login password** | `Micra1234` |

Passwords are **case-sensitive** (`M` uppercase, rest lowercase).

### Connect on first boot (no home Wi‑Fi saved yet)

1. Power the ESP32-S3 and wait for boot (the GPIO1 LED stays off until a scale connects).
2. On your phone or laptop, open Wi‑Fi settings and join **`MicraShotStopperAP`**
   using password **`Micra1234`**.
3. Open a browser to **`http://192.168.4.1`**. Status, live shot, history, and
   the diagnostic log are visible without signing in.
4. Click **Sign in** (or open the Configuration panel) and enter
   **`Micra1234`** to change settings, save your home Wi‑Fi, or run
   maintenance actions.

The SoftAP stays available when there is no home Wi‑Fi saved, and after saved
credentials fail to associate for about **15 seconds** at boot (STA is tried
first). There is no idle SoftAP shutdown timer.

### After home Wi‑Fi (STA) is saved

Once STA credentials are stored and the device joins your network, SoftAP is
stopped and the Web UI is at **`http://<device-ip>`** (find the IP in your
router’s DHCP list, the saved static IP, or serial logs at **115200** baud).
If STA drops after a successful join, the device retries STA only — SoftAP is
**not** raised automatically. Recover SoftAP with USB `AP_START` or a reboot.
Use the same Web UI password **`Micra1234`** (or the password you set under
**Web UI password**). Factory reset restores all values in the table above. If
you lose STA or the UI password, recover over SoftAP (after reboot / `AP_START`)
or USB with the [USB serial CLI](docs/SERIAL_CLI.md).

STA addressing can be **DHCP** (default) or **static IP** from the Wi‑Fi panel.
After any STA save, the new settings stay **pending** until you sign in at the
device IP within **3 minutes**; otherwise the previous (last-known-good)
network settings are restored and STA retries continue. SoftAP does **not**
auto-raise after a successful STA join in that boot — use USB `AP_START` or
reboot if you need the AP again.

## Web UI and Wi-Fi (details)

See **Web UI, Wi-Fi, and API** under [Main features](#main-features) for
diagnostics, virtual paddle control, and the full capability list. Operational
notes:

- The interface cannot change workflow settings during an active cycle.
- Virtual paddle and remote quick rinse require
  `SHOT_STOPPER_ENABLE_REMOTE_CN9=1` on a trusted network. **Stop** works in
  every build when authenticated.
- Each remote cycle is bound to its session and a non-reusable lease.
- Configuration, NVS, Wi-Fi save, and scan operations use a **maintenance
  reservation** that requires stable paddle OFF and open CN9; physical movement
  cancels it. Read-only status and the web UI remain available while the paddle
  is ON.
- `202` responses include a `requestId`; `GET /api/v1/status/{home|settings|admin|debug}` publishes the
  terminal command state (`APPLIED`, `PERSISTED`, `FAILED`, `CANCELED`).
- You may optionally change the default password from the Web UI on a trusted
  network (see [Factory credentials](#factory-credentials-first-use)); it is
  not required to unlock controls.

## Watchdog and CN9 safety

The firmware configures the ESP-IDF Task Watchdog with a 5-second timeout and
`trigger_panic=true`. It subscribes `loopTask`, `scale_worker`, and
`network_manager` separately. No task
feeds another task's watchdog. A registration or feed failure inhibits new
closes, opens CN9, and requests a restart from the control loop. Compilation
fails unless the core enables TWDT panic, IWDT, reboot after panic, and an
IRAM GPTimer handler.

CN9 closes transactionally: `ARMING` is first published with a new generation,
then all deadlines are armed, and only then is the relay energized if the
generation is still current. Opening uses the reverse risk order: the OPEN
electrical level is written first, then timers are stopped. The shortest
deadline also runs through a GPTimer interrupt independently from the loop,
BLE, Wi-Fi, and the `esp_timer` task. Its emergency opening writes the GPIO
register directly from IRAM code, so it does not depend on `digitalWrite()` or
flash cache availability.

Before K1 is energized, the CLOSE command is recorded in RTC memory as a value
and its complement; OPEN is recorded after opening. A WDT reset, panic,
brownout, power glitch, CPU lockup, or reset during CLOSE is retained for
diagnostics, but does not latch the next boot. The Arduino panic callback opens
CN9 immediately through the IRAM GPIO path; `setup()` drives it OPEN again
before initializing Serial, storage, BLE, Wi-Fi, or the safety timers. Once
those subsystems initialize normally, CN9 and the Web UI are immediately
available under their regular runtime rules; no local recovery gesture is
required.

These firmware defenses reduce lockups and races, but a reset cannot open a
welded contact or repair a shorted relay transistor. Containing those faults
requires a normally-open second K2 barrier driven by an external heartbeat
detector with a non-retriggerable limit, plus isolated real-state feedback.
Without K2 and feedback, the Web UI reports `external not configured`, and the
protection must be considered **software only**, not a physical guarantee
against relay or GPIO failures.

## Hardware

```mermaid
flowchart LR
  P[Micra physical paddle] -->|GPIO to GND| E[ESP32-S3]
  E -->|GPIO| D[Relay driver K1]
  D -->|COM/NO| K[Optional safety contact K2 NO]
  K -->|isolated dry-contact chain| C[Micra CN9]
  E -->|validated heartbeat| X[External watchdog and one-shot limit]
  X --> K
  C -. isolated feedback .-> E
  S[Bookoo/Acaia/Felicita scale] <-. BLE .-> E
  W[Phone or computer] <-. Wi-Fi .-> E
```

Use a relay or contact with isolation and ratings suitable for CN9. Never
connect CN9 to GND, VCC, or an ESP32-S3 GPIO, and use COM/NO rather than NC.
Feedback must also be isolated: do not electrically join CN9 and ESP32-S3 GND.
The external detector must drive K2 OPEN when the heartbeat is stuck HIGH,
stuck LOW, absent, or out of frequency; a second relay controlled directly by
the same GPIO does not provide this barrier.

The selected FQBN automatically defines the matching pin block in
`shotStopper.ino`. An active-HIGH **scale-connected LED** on GPIO 1 is part of
the default map:

| Module | Script arch | FQBN extras | Paddle | Relay | Scale LED |
| --- | --- | --- | ---: | ---: | ---: |
| ESP32-S3 N8R4 (8 MB flash, 4 MB QSPI PSRAM) | `n8r4` | `PSRAM=enabled,FlashSize=8M,PartitionScheme=default_8MB` | GPIO 21 | GPIO 38 | GPIO 1 |
| ESP32-S3 N16R8 (16 MB flash, 8 MB OPI PSRAM) | `n16r8` | `PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB` | GPIO 21 | GPIO 38 | GPIO 1 |

Classic ESP32 Dev Module and Arduino Nano ESP32 are **not supported**. Both S3
variants share the same GPIO map; choose `n8r4` or `n16r8` so flash size and
PSRAM bus (QSPI vs OPI) match the module. The scripts have no default board;
`--arch` is asked once and then remembered. The
FQBN enables PSRAM at boot (`BOARD_HAS_PSRAM`); paddle, relay, BLE, safety,
NVS/OTA scratch buffers, and FreeRTOS stacks that touch flash (network manager,
httpd/OTA) stay on internal SRAM. Web UI JSON work buffers and the HTTP parse
arena live in PSRAM and must not run on the control path.
Before energizing CN9, verify every pin for the specific board and the active
polarity of the relay module.

### Scale-connected LED

The firmware drives a simple active-HIGH GPIO LED (default **GPIO 1**) from the
control loop: **HIGH** while a BLE scale is connected and **Settings → Alerts
→ Blue LED while scale connected** is on; **LOW** otherwise. There is no
addressable RGB (WS2812) driver and no extra FreeRTOS task. The LED is
diagnostic only and is never part of the CN9 safety decision.

Override the pin at compile time. It must be output-capable and distinct from
paddle, relay, buzzer, heartbeat, and feedback GPIOs:

```sh
./scripts/build --arch n16r8 \
  --flags "-Werror=deprecated-copy -DSHOT_STOPPER_SCALE_CONNECTED_LED_GPIO=1"
```

GPIO 1 is the board default. Verify the schematic, strapping requirements,
relay integration, and physical board before selecting another pin.

### Enable external heartbeat and feedback

There are no default K2/feedback pins because they depend on the reviewed board
and circuit. Both must be defined; defining only one causes a compilation
error:

- `SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO`: output to the external detector.
- `SHOT_STOPPER_CN9_FEEDBACK_GPIO`: isolated feedback input.
- `SHOT_STOPPER_CN9_FEEDBACK_CLOSED_LEVEL`: optional; defaults to `LOW`.

Example build for an ESP32-S3 where GPIO 16 and GPIO 17 were verified as
free and appropriate on the specific hardware:

```sh
./scripts/build --arch n16r8 \
  --flags "-Werror=deprecated-copy -DSHOT_STOPPER_ENABLE_REMOTE_CN9=1 \
-DSHOT_STOPPER_SAFETY_HEARTBEAT_GPIO=16 -DSHOT_STOPPER_CN9_FEEDBACK_GPIO=17"
```

The README [compile examples](#compile) enable remote CN9 actuation with
`-DSHOT_STOPPER_ENABLE_REMOTE_CN9=1`. Omit that flag for local-only builds.

The firmware checks at compile time that both pins differ from paddle, relay,
and the scale-connected LED pin, and that the heartbeat uses an output-capable GPIO. This does not
replace review of pinout, boot strapping pins, schematics, or electrical
measurements on the actual board.

## Prepare the build environment

The project requires `arduino-cli`, a C++ compiler for host tests, and Node.js
to validate Web UI assets. Optional coverage requires LLVM (`llvm-profdata` and
`llvm-cov`). First confirm that Arduino CLI is available:

```sh
arduino-cli version
```

### Optional static analysis (recommended)

[`Cppcheck`](https://cppcheck.sourceforge.io/) is the recommended optional tool
for static analysis of the C++ firmware. It uses the compilation database
generated by `arduino-cli`. On macOS with [Homebrew](https://brew.sh/), install
it with:

```sh
brew install cppcheck
```

After building a variant (for example, `./scripts/build`), its
`build/n16r8/compile_commands.json` supplies the target-specific include paths
and defines to Cppcheck. Limit analysis to this repository's source files,
rather than the Arduino core or installed third-party libraries, to avoid
irrelevant diagnostics. The analysis script filters Cppcheck to the generated
sketch plus this repository's `shotStopper/` and `libraries/AcaiaArduinoBLE/`
sources. Its versioned suppression list also hides diagnostics from the ESP32
core and installed Arduino libraries that are included transitively, so reports
contain only code maintained in this repository. `AcaiaArduinoBLE` remains in
scope because it is a local library versioned here.

Run Cppcheck with the following script:

```sh
./scripts/static_report
```

It does **not** compile the firmware: the selected build must already exist.
By default it reads `build/n16r8/compile_commands.json`, deletes the previous
`reports/static-analysis/` directory, and replaces it with `cppcheck.txt` and
a small run summary. Pass a build variant and, optionally, another
repository-relative output directory when needed:

```sh
./scripts/static_report build/n8r4 reports/analysis-n8r4
```

For a deeper analysis using the actual Xtensa GCC toolchain, run:

```sh
./scripts/gcc_analyzer
```

This recompiles the firmware with `-fanalyzer`, replacing
`reports/gcc-analyzer/gcc-analyzer.txt` on each run. The main report retains
only diagnostics whose main location is code in this repository; the full
compiler log is available as `gcc-analyzer-raw.txt` for troubleshooting.
Unlike `static_report`, it does compile the firmware and exits non-zero when
the filtered report contains an analyzer diagnostic.

Initialize its configuration only if one does not already exist, then add the
ESP32 board index:

```sh
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Install the toolchain and dependency versions validated by this project:

```sh
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install ArduinoBLE@2.1.0
./scripts/patch_arduinoble.sh
```

`./scripts/patch_arduinoble.sh` is required: stock ArduinoBLE 2.1.0 scans
active at 20/20 ms (100% duty). The patch sets active 40/20 ms (50% duty) so
SCAN_RSP names stay visible while leaving airtime for the Wi-Fi AP, and makes
discovery OOM-safe (`malloc` + placement `new`) so a starved heap drops an
advertisement instead of aborting. The script is idempotent and also converts
a leftover 100/30 patch. Set `ARDUINO_BLE_HOME` if ArduinoBLE is not in the
default Arduino libraries path.

This project is pinned to Arduino-ESP32 **3.3.11** (ESP-IDF 5.5.5). On
**3.3.6+** the core frees BLE controller RAM at boot unless a translation
unit includes `esp32-hal-alloc-ble-mem.h` (ArduinoBLE does not). The vendored
`AcaiaArduinoBLE` library includes that header so `BLE.begin()` succeeds.
Shot Stopper requires ESP32-S3 with PSRAM so Web UI buffers can live in
SPIRAM while the BLE controller stays on internal SRAM. After a core bump,
run the BLE+Web UI hardware gate in [Manual Test Plan](docs/MANUAL_TEST_PLAN.md)
(M72 smoke, M73 short soak).

Idle discovery does not start or stop GAP every 1 s or 3 s. Those timers are
retry (only when scan is down), a software tick for logs, and log throttle.
The radio stays in scan; the controller runs the 40/20 cycle. The only
periodic GAP restart is the 60 s HCI watchdog.

Do not install AcaiaArduinoBLE from Library Manager for this application: the
audited version is included in `libraries/AcaiaArduinoBLE`, and the build
command selects it through `--library`.

## Firmware version

Release version lives in `VERSION` at the repository root (`MAJOR.MINOR.PATCH`).
Bump it manually when publishing a release. The git commit hash is appended
automatically on every build (`1.0.0+abc1234`, or `-dirty` with uncommitted
changes).

Before compiling or running host tests, install Node deps once (for Terser),
then generate the version and gzip Web UI headers:

```sh
npm install
./scripts/gen_version.sh n16r8
node ./scripts/gen_web_ui.js
```

This writes `shotStopper/ShotStopperVersion.h` and
`shotStopper/ShotStopperWebAssetsGzip.h` (both gitignored). The generator reads
`shotStopper/ShotStopperWebAssets.h`, `shotStopper/web/app.js`,
`shotStopper/web/app.css` (JS is minified with
Terser). The installed firmware reports the version on Serial boot, in
`GET /api/v1/status/{home|settings|admin|debug}` as `firmwareVersion`, and in the Web UI footer. `GET /`
(and `/history`, `/log`, `/settings`, `/debug`) serves the SPA HTML as gzip with an
`ETag` derived from that version. `GET /app.js` and `GET /app.css` are gzip with a
long-lived cache and the same ETag family. Use
`curl --compressed` if you fetch HTML/JS/CSS from the command line.

To verify a compiled binary without flashing:

```sh
strings build/n16r8/shotStopper.ino.bin | grep -E '^[0-9]+\\.[0-9]+\\.[0-9]+\\+'
```

## Compile

### Scripts de compilación, carga y monitor serie

Los scripts de `scripts/` simplifican el flujo habitual. **Ningún script rellena
en silencio lo que falte**: cada parámetro se toma, en este orden, del flag con
nombre, de su variable de entorno, del archivo `.shotstopper` en la raíz del
repositorio, o se pregunta en la terminal. En la pregunta, **Enter acepta el
valor sugerido** entre corchetes: puerto USB detectado (o `/dev/cu.usbmodem2101`),
arquitectura `n16r8`, velocidad `115200`, host el último usado (o `192.168.4.1`),
y los flags extra de desarrollo actuales (`-Werror=deprecated-copy`,
`REMOTE_CN9`, buzzer activo). El **token OTA no se guarda ni se sugiere** — hay
que pasarlo por flag o `SHOTSTOPPER_OTA_TOKEN` en cada actualización. Tras
resolver los parámetros los demás valores quedan guardados, así que la segunda
vez basta con `./scripts/bum`.

| Script | Parámetros que necesita | Descripción |
| --- | --- | --- |
| `./scripts/build` | `--arch` (`--flags` opcional) | Genera la versión y el Web UI, compila y deja el resultado en `build/<arquitectura>`. |
| `./scripts/upload` | `--port`, `--arch` | Sube el binario existente de `build/<arquitectura>`; no recompila. |
| `./scripts/monitor` | `--port`, `--speed` | Abre el monitor serie. |
| `./scripts/ota` | `--arch`, `--host`, `--token` | Sube el binario ya compilado al controlador por Wi-Fi, lo verifica y lo deja arrancando. Sin cable USB. |
| `./scripts/bu` | `--port`, `--arch` | Wrapper: build y upload. |
| `./scripts/bum` | `--port`, `--arch`, `--speed` | Wrapper: build, upload y monitor. |
| `./scripts/bo` | `--arch`, `--host`, `--token` | Wrapper: build y ota. |
| `./scripts/static_report` | `--build-dir`, `--output-dir` | Ejecuta Cppcheck sobre una compilation database existente y guarda el reporte. No compila. |
| `./scripts/gcc_analyzer` | `--arch` (`--flags` opcional) | Compila con GCC `-fanalyzer` y guarda el reporte en `reports/gcc-analyzer/`. |
| `./scripts/bsum` | `--port`, `--arch`, `--speed` | Wrapper que ejecuta build, static report, upload y monitor. Si el análisis encuentra diagnósticos, no carga el firmware. |

Parámetros con nombre, en forma larga y corta:

| Flag | Variable de entorno | Significado |
| --- | --- | --- |
| `-p`, `--port` | `SHOTSTOPPER_PORT` | Puerto serial, por ejemplo `/dev/cu.usbmodem2101`. |
| `-a`, `--arch` | `SHOTSTOPPER_ARCH` | `n8r4` o `n16r8` (alias `esp32s3` → `n16r8`). |
| `-s`, `--speed` | `SHOTSTOPPER_SPEED` | Velocidad del monitor serie, por ejemplo `115200`. |
| `-H`, `--host` | `SHOTSTOPPER_HOST` | IP o nombre del controlador para OTA. |
| `-t`, `--token` | `SHOTSTOPPER_OTA_TOKEN` | Token OTA: la contraseña del punto de acceso. |
| `-f`, `--flags` | `SHOTSTOPPER_FLAGS` | Flags extra de compilación, en una sola cadena. |
| `-b`, `--build-dir` | `SHOTSTOPPER_BUILD_DIR_OVERRIDE` | Carpeta de compilación (solo `static_report`). |
| `-o`, `--output-dir` | `SHOTSTOPPER_OUTPUT_DIR` | Carpeta de reportes (solo `static_report`). |
| `-h`, `--help` | — | Muestra la ayuda del script. |

El archivo `.shotstopper` se crea con permisos `600` y está en `.gitignore`.
No persiste el token OTA. Para una terminal sin TTY (CI), define las
variables de entorno o pasa los flags: si falta algo, el script termina con un
error que nombra los parámetros que faltan en lugar de preguntar. Lo mismo
ocurre con `SHOTSTOPPER_NONINTERACTIVE=1`.

```sh
# Primera vez: pregunta lo que falte y lo recuerda
./scripts/bum

# Explícito
./scripts/build --arch n8r4 --flags "-DSHOT_STOPPER_ENABLE_BUZZER=1"
./scripts/upload --port /dev/cu.usbmodem2101 --arch n8r4
./scripts/monitor -p /dev/cu.usbmodem2101 -s 115200

# Build, static report, upload y monitor en una sola orden
./scripts/bsum -p /dev/cu.usbmodem2101 -a n8r4 -s 115200

# Actualización por Wi-Fi, sin cable
./scripts/bo --arch n16r8 --host 192.168.1.50

./scripts/build --help
./scripts/bsum -h
```

From the repository root, install Node deps if needed, generate the version and
Web UI headers, and build for ESP32-S3 N16R8 with:

```sh
npm install
./scripts/gen_version.sh n16r8
node ./scripts/gen_web_ui.js
mkdir -p build/n16r8
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc \
  --warnings all \
  --build-property 'compiler.cpp.extra_flags=-Werror=deprecated-copy -DSHOT_STOPPER_ENABLE_REMOTE_CN9=1 -DSHOT_STOPPER_ENABLE_BUZZER=1' \
  --library libraries/AcaiaArduinoBLE \
  --build-path build/n16r8 \
  shotStopper
```

For N8R4, use `PSRAM=enabled,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB`
and `build/n8r4`, or `./scripts/build --arch n8r4`.

`-DSHOT_STOPPER_ENABLE_REMOTE_CN9=1` exposes virtual paddle and remote quick
rinse over the Web UI and API. **Stop** is always available when authenticated,
even without this flag. Use remote actuation only on a trusted network.

`-DSHOT_STOPPER_ENABLE_BUZZER=1` enables a **passive** piezo (LEDC 2700 Hz).
Use `=2` for an **active** buzzer (GPIO HIGH/LOW). Alerts and debug beeps are
the same either way. Omit the flag or set `=0` for builds without that hardware.

N16R8 uses the normal 16 MB OTA scheme `app3M_fat9M_16MB` (**3 MB** app slot
× 2). N8R4 uses `default_8MB` (**~3.2 MB** × 2). The 4 MB `default` table only
allows **1.25 MB** and cannot hold this firmware; `min_spiffs` is not used.

After compiling, verify the on-disk image fits the OTA slot (optional sanity
check):

```sh
wc -c < build/n16r8/shotStopper.ino.bin
```

## Upload to the ESP32-S3

Connect the board and determine its port:

```sh
arduino-cli board list
```

After compiling, upload by replacing the example port with the port used by
your system. `./scripts/upload` (and `bu` / `bum` / `bsum`) ask for the port
once and remember it. Use `n8r4` if that is the module on the board:

```sh
arduino-cli upload \
  --port /dev/cu.usbmodem2101 \
  --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc \
  --input-dir build/n16r8 \
  shotStopper
```

The generated application image is named `shotStopper.ino.bin`.
`arduino-cli upload --input-dir` is recommended because it uploads the
bootloader, partition table, and application at their correct offsets.

## Update over Wi-Fi (OTA)

Once the controller is installed inside the machine, USB is no longer
convenient. Both partition tables carry **two application slots**, so a new
build can be sent over Wi-Fi and the previous one stays intact as the fallback.

```sh
./scripts/bo --arch n16r8 --host 192.168.1.50
```

`bo` compiles and then runs `./scripts/ota`, which uploads
`build/<arquitectura>/shotStopper.ino.bin`, waits for the controller to verify
it, shows the identity it read back, and asks before flashing. The same thing
is available from **Admin → Firmware update** in the Web UI, as two explicit
steps: *Upload and verify* first, *Flash and restart* only after you have seen
what was verified.

The controller's IP is shown on the Web UI **Admin** and **Diagnostic** pages.
In SoftAP mode it is always `192.168.4.1`.

### What protects the machine

An update must never leave a controller that cannot boot inside a closed
machine, so the firmware refuses far more than it accepts:

- **The running firmware is never overwritten.** The upload goes to the
  inactive slot. A power cut, a lost Wi-Fi link, a browser tab closed
  mid-transfer, or an outright wrong file all leave the working firmware
  untouched and the boot selection unchanged.
- **Nothing reaches flash until the header proves itself.** The first 288 bytes
  must be an ESP32-S3 application image built against the Arduino core.
- **The image must say it is a Shot Stopper build for this board.** Every build
  embeds a marker with its board architecture and version. An `n8r4` image is
  rejected by an `n16r8` controller, and vice versa, and a build older than the
  running one is rejected unless the request explicitly allows a downgrade.
- **The whole image is checksummed** before it is offered for flashing, and its
  identity is read back **from flash** again immediately before the boot slot
  is switched.
- **Updates only run while the machine is idle.** CN9 open, no active cycle,
  paddle off, state Ready. This is re-checked every 64 KiB during the transfer
  and the update is cancelled if the machine stops being idle. The unsafe WebUI
  override, which can relax ordinary settings, is never accepted here.
- **The restart is the ordinary safe restart**: CN9 is opened first, and the
  reset waits for the machine to be idle.
- **A new firmware has to earn its place.** It boots on probation and is made
  permanent only once it has been up for 15 s with its HTTP server listening,
  which means the boot sequence, the control task, Wi-Fi and the web stack all
  came up and the machine can be updated again over the air. If it panics,
  hangs, or never brings up HTTP within 3 minutes, the previous slot comes
  back. The one case where the new image is kept anyway is when no other slot
  holds a bootable application, because restarting would then leave nothing to
  run at all.
- **Settings survive.** Wi-Fi credentials, configuration, calibration, presets,
  and shot history live in NVS, which an update does not touch.

Authentication uses the SoftAP password in an `X-OTA-Token` header. A
controller still on the published factory password cannot be updated over the
air at all: change it first from **Admin → AP password**.

The relay's hard CN9 limit runs on a hardware timer whose interrupt handler
lives in IRAM, so it stays armed even while flash is being written and the
cache is disabled.

Probation is enforced below the firmware, by the bootloader, which is what makes
it survive a firmware that is broken enough not to run at all. It relies on
three settings of the ESP32 Arduino core the build is compiled against, and the
firmware refuses to compile without the watchdog ones:

| Setting | Value | What it gives us |
| --- | --- | --- |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | `y` | A slot that resets before being marked valid is abandoned and the previous one is booted. |
| `CONFIG_ESP_TASK_WDT_PANIC` | `y` | A task that stops responding panics and resets, which is what hands control back to the rollback above. |
| `CONFIG_BOOTLOADER_WDT_TIME_MS` | `9000` | An image that hangs *before* reaching the firmware is still reset, and so still rolled back. |

### If an update fails

Nothing needs to be done: the controller keeps running the firmware it already
had. The Web UI shows the reason, and the same event is recorded in the
diagnostic log (`OTA upload rejected`, `OTA rollback armed`, …).

A USB upload is always still possible and is the way out of any state. It
writes `boot_app0.bin` at `0xe000` alongside the application, which resets the
boot selection to the first slot, so `./scripts/upload` after any number of OTA
updates lands on exactly the firmware you just built. See
[Emergency recovery](docs/EMERGENCY_RECOVERY.md).

## Serial monitor

Connect the board over USB and list available ports:

```sh
arduino-cli board list
```

On macOS the native USB CDC port is usually `/dev/cu.usbmodemXXXX` (use the
`cu.*` device, not `tty.*`, for monitoring). UART-bridge boards may still
appear as `/dev/cu.usbserial-XXXX`. On Linux it is often `/dev/ttyACM0` or
`/dev/ttyUSB0`.

Open the monitor with `arduino-cli`, replacing the port with yours.
Use **115200** for ESP32-S3 boot messages and Shot Stopper logs and CLI (same
rate after the app starts). Commands are listed in
[USB serial CLI](docs/SERIAL_CLI.md). Debug paddle/CN9/Wi-Fi traces are
**off** by default; enable them from Debug (**Serial debug output**) or
`SERIAL_DEBUG_ON`.

```sh
arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200
```

You should see CLI replies immediately. With serial debug on, lines such as
`Shot Stopper Micra …` and `Goal weight: …` also appear.
Press **RST** on the board if the monitor was already open. Exit with
`Ctrl+C`.

### Troubleshooting serial output

Not every baud rate works on every board. The correct speed depends on the
USB‑serial chip (CP2102, CH340, FTDI, native USB CDC), the ESP32-S3 module, and
whether you are reading the **bootloader** or the **running firmware**. If the
monitor shows garbage (random symbols and repeated characters), try another rate and press **RST** after
each change.

| Baud rate | Typical use |
| --- | --- |
| **115200** | Shot Stopper application logs and CLI (this firmware); also ESP32-S3 USB CDC and UART boot messages |
| **74880** | Classic ESP32 ROM bootloader only (not used on ESP32-S3) |
| **57600** | Some CH340 / clone adapters and older sketches |
| **38400** | Fallback on misconfigured or bridged setups |
| **19200** | Rare; worth trying if nothing else is readable |
| **230400** | High-speed debug builds (not used by this project) |

Example — try another rate:

```sh
arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200
```

If output is still unreadable at every rate:

- Confirm the port with `arduino-cli board list` and use the `cu.*` device on
  macOS.
- Close other programs that may hold the port (Arduino IDE, another monitor).
- Try a different USB cable or port (must be data, not charge-only).
- Re-flash the firmware and watch **115200** or **74880** during reset for
  partition or boot errors.

If you see `Image length … doesn't fit in partition length …`, confirm the
FQBN matches the module (`app3M_fat9M_16MB` for N16R8, `default_8MB` for
N8R4; see [Compile](#compile)) and upload again.

## Automated tests

Run the tests before uploading firmware:

```sh
npm install
./libraries/AcaiaArduinoBLE/tests/run_host_tests.sh
./shotStopper/tests/run_host_tests.sh
node ./shotStopper/tests/check_web_assets.js
node ./shotStopper/tests/check_firmware_size.js build/n16r8/shotStopper.ino.bin
```

The firmware size check verifies the application binary fits the OTA app slot
for that target (`3145728` bytes on N16R8, `3342336` on N8R4). Skip it if you
have not compiled yet, or pass another `.bin` path as the first argument.

To generate the stopper coverage report when LLVM is installed:

```sh
./shotStopper/tests/run_coverage.sh
```

Automated tests do not replace electrical, RF, power-loss, or
[manual test plan](docs/MANUAL_TEST_PLAN.md) verification.

The stopper suite includes host fault injection for a timeout during `ARMING`,
GPTimer failure, task watchdog failure, stuck/disconnected feedback, and
heartbeat faults. Bench and HIL testing of the specific circuit remains
mandatory before real use.

## Additional documentation

- [FAQ (Spanish)](docs/FAQ.md) — confusing automatic behaviors, defaults, and compatibility
- [USB serial CLI](docs/SERIAL_CLI.md)
- [Manual test plan](docs/MANUAL_TEST_PLAN.md)
- [Local AcaiaArduinoBLE library](libraries/AcaiaArduinoBLE/README.md)

## License and acknowledgements

The project retains the MIT license in [LICENSE](LICENSE).

Micra Shot Stopper is a derivative work that **would not exist without**
[tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE): the
original proved BLE brew-by-weight stop and shared the core scale protocol work.
This repository acknowledges that project and the contributors listed in the
[local library documentation](libraries/AcaiaArduinoBLE/README.md#acknowledgement).
The application firmware, safety model, Web UI, and Micra-specific workflow are
substantial new work on top of that foundation.

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
  compile-time pin maps, polarity, CN9 limits, and workflow parameters — so
  that paddle readback, CN9 control, and automatic stop behavior match your
  hardware. GPIO and other safety-critical pin assignments are **not**
  configurable from the Web UI; they must be set in source and verified at
  build time (see [FAQ](docs/FAQ.md)).

**Espresso machines are inherently hazardous.** A machine such as the La
Marzocco Linea Micra contains **pressurized boilers, hot water, and steam at high
temperature**. Adding automatic or remote control — including brew-by-weight
stop, relay actuation, Wi‑Fi commands, and timer-based limits — can increase
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

### Development assistance

This project was developed with substantial assistance from artificial
intelligence tools. AI helped with design, implementation, documentation, and
testing workflows; human review, hardware validation, and safety judgment remain
the author’s responsibility. Use on real espresso equipment only after you have
verified wiring, isolation, and behavior on your own setup.

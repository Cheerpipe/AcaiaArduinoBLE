# Manual Test Plan

Perform all relay and CN9 tests on a bench first. Do not connect CN9 to the machine until the electrical checks pass.

| ID | Procedure | Expected result |
| --- | --- | --- |
| M01 | Power the controller with the paddle OFF. | Relay is open at boot; after debounce, state is `READY`. |
| M02 | Power it with paddle ON, then release it. | CN9 stays open until a stable OFF is detected. |
| M03 | From Ready, turn paddle ON for less than the rinse gesture threshold, then OFF. | Rinse starts, CN9 remains closed for configured rinse duration, then opens. |
| M04 | Hold paddle ON past brew confirmation with a connected scale. | State becomes `BREW`; optional Bookoo beep is heard only when enabled. |
| M05 | Repeat M04 with beep disabled. | Brew starts normally and no beep command is sent. |
| M06 | Turn paddle OFF during brew/manual. | CN9 opens immediately; next cycle requires normal rearm. |
| M07 | Leave a cycle ON until the configured wall time. | CN9 opens no later than the configured limit, never later than 60 s. |
| M08 | Disconnect the scale during brew. | The cycle degrades to manual; paddle OFF and hard limit still open CN9. |
| M09 | On a deliberately opt-in build with `SHOT_STOPPER_ENABLE_REMOTE_CN9=1`, open Web UI and use Virtual paddle ON/OFF. | The visual switch changes position and follows normal state-machine behavior. On the default build both close-producing controls are disabled and rejected by the API. |
| M10 | Change a workflow field during brew, rinse, qualifying and manual states. | UI is disabled and API rejects each change; current snapshot is unchanged. |
| M11 | Save valid workflow settings while Ready, restart, and reopen UI. | Values persist and apply to the next cycle only. |
| M12 | Enter a relationship-invalid timing configuration. | Server rejects the complete transaction; no field changes. |
| M13 | Boot without STA credentials. | AP is reachable at `http://192.168.4.1/` for three minutes. |
| M14 | Save valid STA credentials and restart. | AP remains off; Serial prints DHCP address; UI stays available at that address. |
| M15 | Save invalid/unreachable STA credentials and restart. | After the attempt timeout, recovery AP becomes available. |
| M16 | Scan Wi-Fi in Ready, then start a physical cycle. | Scan is cancelled; control and relay behavior are unaffected. |
| M17 | On the remote-control opt-in build, leave a browser virtual paddle ON, then close/disconnect it. | Heartbeat timeout safely opens CN9. |
| M18 | Reset or remove controller power while measuring relay COM/NO. | Contact remains open; there is no unintended close pulse. |
| M19 | From Ready, press **Reset learned stop offset**, confirm, then restart. | The learned offset is reset and remains at the default 1.5 g; a pending post-shot analysis cannot overwrite it. |
| M20 | Open the Web UI status with the external safety macros disabled. | `CN9 Safety` reports timers and WDT ready, plus `external not configured`; this build is identified as software-only protection. |
| M21 | On an isolated bench load, use an instrumented test build to stop `loopTask` immediately after CN9 closes. Do not attach OpenOCD during the measurement. | GPTimer/K2 opens CN9 within the configured measured limit and TWDT reboots the board within its 5 s timeout; the boot never recloses CN9. |
| M22 | Freeze only `scale_worker` while leaving control responsive. | Its own TWDT subscription causes a reboot within 5 s; CN9 opens on or before reset and does not resume afterward. |
| M23 | Freeze only `network_manager` while leaving control responsive. | Its own TWDT subscription causes a reboot within 5 s; no network task can directly restart or close CN9. |
| M24 | Build with K2/feedback enabled, disconnect the isolated feedback input, and request a close. | Feedback fails to confirm within 100 ms; K1 opens and safety enters `LOCKOUT`. |
| M25 | Force feedback to the CLOSED level before boot and before a close request. | The supervisor reports `FEEDBACK_STUCK_CLOSED`, remains in `LOCKOUT`, and never energizes K1. |
| M26 | While K2 is energized, hold heartbeat permanently LOW, then permanently HIGH, and finally disconnect it. | The external detector opens K2 within its independently specified timeout in all three cases. |
| M27 | Simulate a welded K1 or shorted K1 driver using the isolated bench fixture, then remove heartbeat. | K2 still opens the series CN9 path within its measured non-retriggerable limit. |
| M28 | Repeat M01, M02, M07, M18 and M21-M27 on ESP32, ESP32-C3 and ESP32-S3. | Every target passes with measured timings recorded; C3 shows no idle-task starvation or false WDT resets. |
| M29 | Run brownout, power-cut, Wi-Fi/RF stress and a 72 h soak followed by 10,000 relay cycles on the isolated fixture. | No spontaneous close, over-limit close, resumed cycle after reset or undiagnosed feedback mismatch occurs. |
| M30 | With the default build, authenticate from two browsers and inspect the Actions panel and API. | Virtual paddle and rinse remain disabled/forbidden; either authenticated session can issue STOP, which only opens CN9. |
| M31 | With remote control deliberately enabled, start a Web cycle from browser A. Continue heartbeats only from browser B, then logout/expire A. | Browser B cannot renew or turn off A's owned paddle lease; loss of A safely opens CN9. Browser B can still issue emergency STOP. |
| M32 | While Ready, request each configuration, Wi-Fi, scan and factory operation, then move the physical paddle before its maintenance settle time. | The operation is canceled, CN9 stays open, state requires physical OFF, and no partial runtime setting is applied. |
| M33 | Disconnect or freeze the scale before its first weight, then repeat after allowing the last weight to become older than 1 s. | The shot stays available as manual control but never performs a weight-based automatic stop. |
| M34 | Inject an isolated test stream containing NaN, out-of-range weight and an implausible weight jump. | Samples are rejected, the active automatic cycle degrades to manual on implausible slew, and CN9 remains governed by paddle/deadlines. |
| M35 | Force the scale-command and event queues full while ending a shot. | CN9 opens immediately; the control loop never blocks. The scale STOP is retried within its bounded window and drop counters become visible. |
| M36 | Fail AP creation and HTTP startup independently, then remove each fault without rebooting. | Partial startup rolls back and retries with bounded backoff until the AP/UI recovers; control remains responsive and fail-open throughout. |
| M37 | Provision two factory-reset devices and record the Serial output. | Each prints a valid, different AP/UI password; neither accepts a shared/default password. |
| M38 | Force CPU-frequency setup, EEPROM initialization and BLE initialization failures one at a time on an instrumented build. | Frequency/safety initialization failure prevents CN9 close; EEPROM failure uses volatile safe defaults without network persistence; BLE failure permits only local manual operation. |
| M39 | During a 72 h target-specific soak, record `/api/v1/status` health fields and firmware size on every build. | Loop/task age and stack remain bounded, heap minimum/largest block do not trend toward exhaustion, drops are explained, and every `.bin` stays below the CI budget. |
| M40 | Boot with both WS2812B indicators connected on the configured, electrically verified data GPIOs. | Both pixels start without relay movement; the scale pixel blinks blue during initialization and no LED activity closes CN9. |
| M41 | Connect, disconnect, and deliberately stall the scale worker on an instrumented build. | The scale LED changes between solid green, solid red, slow yellow, and fast red according to the documented mapping; CN9 behavior remains controlled by the state machine. |
| M42 | Exercise Ready, qualifying, brew, rinse, manual no-scale, and timer-only workflows. | The stopper LED uses solid/medium/slow/fast patterns and green/salmon palettes exactly as documented, without stale queued colors. |
| M43 | Trigger maintenance, `REQUIRES_OFF`, watchdog failure, and safety lockout separately. | Blue, amber, and fast-red overrides take priority over workflow colors; safety faults still open or inhibit CN9 independently of the LEDs. |
| M44 | Disconnect or short each LED data wire in turn, then repeat a normal paddle stop and the 60-second hard-limit test. | Indicator failure cannot delay the control loop, keep CN9 closed, or affect either hard deadline. |

Record firmware version, board/FQBN, scale model/firmware, measured timings, relay continuity and pass/fail evidence for every run.

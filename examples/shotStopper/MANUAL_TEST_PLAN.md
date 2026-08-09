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
| M07 | Leave a cycle ON until the configured wall time. | CN9 opens no later than configured limit, never later than 50 s. |
| M08 | Disconnect the scale during brew. | The cycle degrades to manual; paddle OFF and hard limit still open CN9. |
| M09 | Open Web UI and use Virtual paddle ON/OFF. | The visual switch changes position and follows normal state-machine behavior. |
| M10 | Change a workflow field during brew, rinse, qualifying and manual states. | UI is disabled and API rejects each change; current snapshot is unchanged. |
| M11 | Save valid workflow settings while Ready, restart, and reopen UI. | Values persist and apply to the next cycle only. |
| M12 | Enter a relationship-invalid timing configuration. | Server rejects the complete transaction; no field changes. |
| M13 | Boot without STA credentials. | AP is reachable at `http://192.168.4.1/` for three minutes. |
| M14 | Save valid STA credentials and restart. | AP remains off; Serial prints DHCP address; UI stays available at that address. |
| M15 | Save invalid/unreachable STA credentials and restart. | After the attempt timeout, recovery AP becomes available. |
| M16 | Scan Wi-Fi in Ready, then start a physical cycle. | Scan is cancelled; control and relay behavior are unaffected. |
| M17 | Leave a browser virtual paddle ON, then close/disconnect it. | Heartbeat timeout safely opens CN9. |
| M18 | Reset or remove controller power while measuring relay COM/NO. | Contact remains open; there is no unintended close pulse. |
| M19 | From Ready, press **Reset learned stop offset**, confirm, then restart. | The learned offset is reset and remains at the default 1.5 g; a pending post-shot analysis cannot overwrite it. |

Record firmware version, board/FQBN, scale model/firmware, measured timings, relay continuity and pass/fail evidence for every run.

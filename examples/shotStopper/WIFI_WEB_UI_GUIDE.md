# Wi-Fi and Web UI Guide

## Access modes

The controller stores Wi-Fi credentials in NVS. On boot it behaves as follows:

1. With saved STA credentials, it tries the configured Wi-Fi network for up to 15 seconds. A successful DHCP lease starts the Web UI permanently at the IP printed on Serial.
2. If STA credentials are missing, or the connection fails, it starts the password-protected recovery AP `MicraShotStopperAP` at `http://192.168.4.1/` for three minutes. Its factory password is `Micra1234`.
3. Opening and authenticating to the UI keeps a recovery session available. The AP is intentionally disabled after its timeout when it is not in use.

The device never enables AP and STA simultaneously. The UI is served only over HTTP on the selected local network; it has no Internet exposure unless the local network itself exposes it.

## Using the UI

Sign in with the AP/UI password. The Status section displays the controller state, CN9 state, active or previous extraction time, scale availability and current or final weight.

The Virtual paddle is a two-position ON/OFF switch. It follows the same state machine as the physical paddle, while a physical paddle change always wins. A Web paddle has a heartbeat safety stop if its browser disappears. **Stop shot** opens CN9 immediately; it does not change workflow settings.

Workflow settings can be saved only in `READY`. They are locked in qualifying, brew, rinse and manual-cycle states. All fields are validated as one transaction:

| Setting | Allowed values |
| --- | --- |
| Target weight | 10–200 g |
| Rinse gesture | 100–5,000 ms |
| Rinse duration | 500–10,000 ms |
| Brew confirmation | 500–10,000 ms |
| Minimum automatic stop | 1,000–30,000 ms |
| CN9 limit | 5,000–50,000 ms |

The required relationship is `rinse gesture < brew confirmation < minimum auto-stop < CN9 limit`; rinse duration must not exceed the CN9 limit. The 50-second limit is hard-coded and cannot be increased.

**Beep when brew is confirmed** controls the optional Bookoo beep issued after a confirmed automatic brew. It has no effect in timer-only mode, and it never changes the scale connection state or performs a tare.

**Reset learned stop offset (1.5 g)** asks for confirmation, cancels any pending post-shot calibration analysis, restores the default stop offset, and persists it. It is available only while Ready.

Use **Scan networks** while Ready to list nearby networks, select one, enter its password, then choose **Save and restart**. Hidden networks can be entered manually. Passwords are never returned by the API or written to the log.

## Safety and diagnostics

The Web UI only reads bounded snapshots and queues fixed-size commands. It never accesses GPIO, the relay, BLE, or the control state machine directly. HTTP, Wi-Fi scans, DHCP and NVS writes run outside the control loop. The log contains state transitions, relay actions, scale connection events and accepted/rejected commands; weight samples are omitted deliberately.

The UI is a convenience control surface, not a replacement for the physical safety checks. Verify CN9 continuity and relay-open behavior before connecting a machine.

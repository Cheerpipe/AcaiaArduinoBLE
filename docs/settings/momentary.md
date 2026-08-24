# Momentary

How a **momentary** brew switch tells the stopper to start or stop a shot.
This page applies to firmware compiled as `SHOT_STOPPER_MACHINE_TYPE=1`
(momentary) or `2` (momentary + reed). Those builds show **Switch** and
hide **Paddle** and **Quick rinse**. Paddle / latch builds do the reverse.
The groups are mutually exclusive.

The relay **mirrors the switch 1:1**. If you hold 234 ms, K1 is closed for
those 234 ms while you hold. A long hold is copied in full. The firmware
does not replay, delay, or replace your gesture. The only extra pulse is
the **auto-stop pulse** it sends when a weight cut (or a safety wall) needs
to toggle the group.

**Start/stop on** chooses when firmware treats the shot as started or
stopped (tare, timer). It does not change the 1:1 relay mirror.

| Mode | Default | When start/stop fires |
| --- | --- | --- |
| **Button press** | yes | On the debounced press. If the hold then exceeds **Single-press limit**, that edge is undone (not a start/stop). Release does not toggle again. |
| **Button release** | no | On release, and only if the hold is no longer than **Single-press limit**. A longer hold is mirror-only (for example a machine rinse). |

Without a scale, the stopper does not know if the group is running. It stays
fully manual: mirror only, no automatic cut.

Related: [Paddle](paddle.md), [Quick rinse](quick-rinse.md),
[Hardware](../HARDWARE.md).

## Parameters

| Setting | Default | Values | Effect |
| --- | --- | --- | --- |
| **Auto-stop pulse (ms)** | 300 | 50–1000 | Length of the pulse the firmware sends to mimic a single button press when it needs to stop the brew automatically (target weight, time walls). |
| **Single-press limit (ms)** | 1000 | 100–5000 | Longest hold that still counts as a single press (start or stop). Applies in **Button press** and **Button release**. A longer hold is not a start/stop (for example a machine rinse): the relay still mirrors it. In press mode the tentative start/stop is undone and, on reed builds, a confirm-timeout grace runs before reed is canonical again. In release mode the edge is never applied. |
| **Start/stop on** | Button press | Button press / Button release | When firmware treats the shot as started or stopped, and when the reed confirm window starts. Does not change the 1:1 relay mirror. |
| **Reed confirm timeout (s)** | 1.0 | 0.2–5 | Momentary+reed only (`SHOT_STOPPER_MACHINE_TYPE=2`). How long after the Start/stop on edge the machine may stay Assumed on while the reed is still off (or Assumed off while the reed is still on). The clock starts on that press or release, not when the relay mirrors the hold. If the reed matches sooner, confirm immediately. When the timeout elapses, confirm the actual reed. |

Reed is polled every control loop (`digitalRead` plus 30 ms debounce). A
stable level is not missed. Outside an assumed window, machine state is
the reed: off → Confirmed off, on → Confirmed on. Assumed on/off exists
only for the button → solenoid → reed lag after that start/stop edge, up
to this timeout. If a press exceeds Single-press limit, that start/stop is
undone (back to the pre-press view) and the same timeout is a grace window
before reed is canonical again. Without a reed, firmware just restores the
pre-press inferred state.

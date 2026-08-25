# Momentary

How a **momentary** brew switch tells the stopper to start or stop a shot.
This page applies to firmware compiled as `SHOT_STOPPER_MACHINE_TYPE=1`
(momentary) or `2` (momentary + reed). Those builds show **Switch** and
hide **Paddle** and **Quick rinse**. Paddle / latch builds do the reverse.
The groups are mutually exclusive.

The relay **mirrors the switch 1:1** while a start-guard is not blocking.
If you hold 234 ms, K1 is closed for those 234 ms while you hold. A long
hold is copied in full. The firmware does not replay, delay, or replace
your gesture. The only extra pulse is the **auto-stop pulse** it sends when
a weight cut (or a safety wall) needs to toggle the group.

If **Avoid BBW shot without scale** is Armed (BBW on, no usable scale) or
**Require cup to start** would reject the start, the relay does **not**
mirror: K1 stays open for that whole hold, even after the guard goes Idle
mid-press. Release, then press again with the guard Idle / a cup present,
and the 1:1 mirror resumes. See [No-scale BBW](no-scale-bbw.md) and
[Cup protection](../features/cup-protection.md).

**Start/stop on** chooses when firmware treats the shot as started or
stopped (tare, timer). It does not change the 1:1 relay mirror, except
that a blocked start also leaves K1 open.

| Mode | Default | When start/stop fires |
| --- | --- | --- |
| **Button press** | yes | On the debounced press. If the hold then exceeds **Single-press limit**, that edge is undone (not a start/stop). Release does not toggle again. |
| **Button release** | no | On release, and only if the hold is no longer than **Single-press limit**. A longer hold is mirror-only (for example a machine rinse). |

Without a scale, or with brew by weight off, the stopper does not send
weight cuts. The 1:1 relay mirror still copies the switch. **Max BBW time
does not apply.** The firmware **60 s** cap can still pulse a running group:
on reed builds if the reed is on; on switch-only builds only if the state
is Confirmed on. Without Confirmed on (including tap-start with no scale),
firmware does not pulse; the next press is a new Start. Holding the switch
for 60 s still opens K1 (electrical cap).

Related: [Paddle](paddle.md), [Quick rinse](quick-rinse.md),
[Hardware](../HARDWARE.md).

## Parameters

| Setting | Default | Values | Effect |
| --- | --- | --- | --- |
| **Auto-stop pulse (ms)** | 300 | 50–1000 | Length of the pulse the firmware sends to mimic a single button press when it needs to stop the brew automatically (target weight, time walls). |
| **Single-press limit (ms)** | 1000 | 100–5000 | Longest hold that still counts as a single press (start or stop). Applies in **Button press** and **Button release**. A longer hold is not a start/stop (for example a machine rinse): the relay still mirrors it. In press mode the tentative start/stop is undone and, on reed builds, a confirm-timeout grace runs before reed is canonical again. In release mode the edge is never applied. |
| **Start/stop on** | Button press | Button press / Button release | When firmware treats the shot as started or stopped, and when the reed confirm window starts. Does not change the 1:1 relay mirror except when a start-guard blocks forwarding. |
| **Assume idle when the scale connects** | ON | ON / OFF | Switch-only (`SHOT_STOPPER_MACHINE_TYPE=1`). When the scale connects, treat the group as idle (Confirmed off). Does not pulse the relay. Skipped while a brew cycle is active. |
| **Shot reaction timeout (s)** | 12 | 3–30; `0` in JSON is the compiled 12 s | Switch-only. How long a quiet pan after Start may stay Assumed on before becoming Assumed off. Does not pulse the relay. Late espresso-like flow from Assumed off still confirms ON. If Assumed on/off lasts until the firmware hard cap (60 s, `HARD_MAX_CIRCUIT_CLOSED_MS`) with a live scale and net mass still within 1 g of the shot baseline (noise, not espresso-like flow), firmware settles to Confirmed off without a pulse or beep so the next press is a new Start. |
| **Reed confirm timeout (s)** | 1.0 | 0.2–5 | Momentary+reed only (`SHOT_STOPPER_MACHINE_TYPE=2`). How long after the Start/stop on edge the machine may stay Assumed on while the reed is still off (or Assumed off while the reed is still on). The clock starts on that press or release, not when the relay mirrors the hold. If the reed matches sooner, confirm immediately. When the timeout elapses, confirm the actual reed. |

Reed is polled every control loop (`digitalRead` plus 30 ms debounce). A
stable level is not missed. Outside an assumed window, machine state is
the reed: off → Confirmed off, on → Confirmed on. That includes boot with
the reed already on, and K1 tripped or lockout. Firmware auto-cut still
waits for one stable off this boot so a stuck-ON reed is not pulsed.
Assumed on/off exists only for the button → solenoid → reed lag after that
start/stop edge, up to this timeout. If a press exceeds Single-press
limit, that start/stop is undone (back to the pre-press view) and the same
timeout is a grace window before reed is canonical again. Without a reed,
firmware just restores the pre-press inferred state.

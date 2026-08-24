# Momentary

How a **momentary** brew switch tells the stopper that the machine started.
Choose the edge in **Settings → Machine and scale → Momentary**
(`momentaryStartEdge`). It is **machine-level**, not per-preset. Default is
**On release**.

This page applies to firmware compiled as `SHOT_STOPPER_MACHINE_TYPE=1`
(momentary) or `2` (momentary + reed). Those builds show **Momentary** and
hide **Paddle** and **Quick rinse**. Paddle / latch builds do the reverse.
The groups are mutually exclusive.

The stopper does not watch the group head directly on a pulse-to-toggle
machine. It infers “running” from a synthetic paddle ON that brew already
understands. This setting only changes **when** that synthetic ON is raised.
Tare, shot time, and rinse-window timing all follow that report. Brew, scale,
and tare policy are unchanged.

## Parameters

| Setting | Default | Values | Effect |
| --- | --- | --- | --- |
| **Machine starts** | On release | On release / On press | When the stopper treats the machine as running. |

Stop and start **pulses** are compile-time (`SHOT_STOPPER_STOP_PULSE_MS`,
`SHOT_STOPPER_MAX_SINGLE_PRESS_MS`). Diagnostic lists them under compile flags.
Idle long-press (drive the circuit without a brew) only applies with
**On release**, because **On press** already started the brew on the down edge.

## On release (default)

1. Press the switch. Nothing is reported yet.
2. Release a **short** press → synthetic ON. Tare and shot/rinse clocks start.
3. The matching release of a later short press stops (or defers stop inside
   the rinse gesture window, same as before).

This matches a pulse-to-toggle group that latches on the click, not on the
finger-down.

## On press

1. Press the switch (after debounce) → synthetic ON immediately. Tare and
   shot/rinse clocks start on the down edge.
2. Releasing **that same** start press does **not** stop and does not count
   as a rinse.
3. The next short press is the stop (after the rinse window, or deferred
   inside it).

Use this when the machine actually starts brewing the moment the switch is
held, so the stopper must tare and time from that instant.

The electrical start pulse cannot yet replay the press duration (the finger
is still down), so start uses the compiled stop-pulse length
(`SHOT_STOPPER_STOP_PULSE_MS`). Pulse length is compile-time, not a Settings
field; Diagnostic shows it under compile flags.

Related: [Paddle](paddle.md), [Quick rinse](quick-rinse.md),
[Hardware](../HARDWARE.md).

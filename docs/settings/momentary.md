# Momentary

How a **momentary** brew switch tells the stopper to start or stop a shot.
This page applies to firmware compiled as `SHOT_STOPPER_MACHINE_TYPE=1`
(momentary) or `2` (momentary + reed). Those builds show **Switch** and
hide **Paddle** and **Quick rinse**. Paddle / latch builds do the reverse.
The groups are mutually exclusive.

The relay **mirrors the switch 1:1**. If you hold 234 ms, K1 is closed for
those 234 ms while you hold. A long hold is copied in full. The firmware
does not replay, delay, or replace your gesture. The only extra pulse is
the **stop pulse** it sends when a weight cut (or a safety wall) needs to
toggle the group.

A press no longer than **Single press max** is start or stop. A longer hold
is still mirrored (so a machine-native rinse still works) but is ignored as
start/stop.

Without a scale, the stopper does not know if the group is running. It stays
fully manual: mirror only, no automatic cut.

Related: [Paddle](paddle.md), [Quick rinse](quick-rinse.md),
[Hardware](../HARDWARE.md).

## Parameters

| Setting | Default | Values | Effect |
| --- | --- | --- | --- |
| **Stop pulse (ms)** | 300 | 50–1000 | Length of the firmware off pulse at weight cut. |
| **Single press max (ms)** | 1000 | 100–5000 | Max hold that still counts as start/stop. Longer holds are mirrored only. |

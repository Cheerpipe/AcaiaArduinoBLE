# No-scale BBW

Machine-level guard that **blocks a full automatic shot** when brew by weight
is on and there is no usable scale. **On by default.**

The Web UI label is **Avoid BBW shot without scale**, under
**Settings → Machine and scale**. The ON/OFF switch is also on
**Home → Quick Settings** (read-only when brew by weight is off).

## When it applies

Only while **Brew by weight** is on and the scale is missing or not usable.
With BBW off, this guard does nothing.

A long activator ON (paddle or momentary switch) **longer than the quick
rinse gesture** does not close the machine circuit. A shorter paddle ON→OFF,
or a momentary idle long-press with **Enable rinse** on, is still a rinse:
machine circuit closes (paddle) or firmware pulses start (momentary) and the
guard goes **Idle**. With Enable rinse off, momentary long-press while Armed
is not forwarded to the relay; after Idle, the next press mirrors 1:1 as a
manual no-scale shot (or a machine-native long hold). The no-scale buzzer
cue still plays on activator ON (brew or rinse) whenever BBW is on and the
scale is missing.

The next start after a blocked shot or an Armed rinse runs as a **manual
no-scale** shot. The guard re-arms on boot, when the scale becomes available,
or after **Last shot cooldown**.

## Parameters

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Avoid BBW shot without scale** | ON | ON / OFF | Armed: a long activator ON does not close the machine circuit. A rinse gesture (paddle short ON→OFF, or momentary idle long-press with Enable rinse) still rinses and consumes Armed. With Enable rinse off, a momentary hold stays open and then consumes Armed. |
| **Last shot cooldown (min)** | 60 min | 5–240 min | After a blocked start, an Armed rinse, or a finished (non-rinse) shot, wait this long before the guard re-arms. Boot and scale reconnect re-arm immediately. |

Status shows `Off` / `Armed` / `Idle`.

## Example

BBW is on, the Bookoo is still in the drawer. A long paddle or switch ON
plays the local buzzer cue and leaves machine circuit open. On paddle, a short
ON→OFF still rinses. On momentary with Enable rinse on, an idle long-press
rinses and consumes Armed. After that, the next long start is a manual shot
without a scale. When the scale connects, the guard re-arms immediately.

Related: [Quick rinse](quick-rinse.md), [Momentary](momentary.md),
[Brew by weight](../features/brew-by-weight.md), [Alerts](../alerts.md)
(ATM / manual-no-scale).

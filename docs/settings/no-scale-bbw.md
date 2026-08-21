# No-scale BBW

Machine-level guard that **blocks a full automatic shot** when brew by weight
is on and there is no usable scale. **On by default.**

The Web UI label is **Avoid BBW shot without scale**, under
**Settings → Machine and scale**. The ON/OFF switch is also on
**Home → Quick Settings** (read-only when brew by weight is off).

## When it applies

Only while **Brew by weight** is on and the scale is missing or not usable.
With BBW off, this guard does nothing.

A paddle ON **longer than the quick rinse gesture** does not close CN9. A
shorter ON→OFF is still a rinse: CN9 closes and the guard goes **Idle**. The
no-scale triple beep still plays on paddle ON (brew or rinse) whenever BBW is
on and the scale is missing.

The next start after a blocked shot or an Armed rinse runs as a **manual
no-scale** shot. The guard re-arms on boot, when the scale becomes available,
or after **Last shot cooldown**.

## Parameters

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Avoid BBW shot without scale** | ON | ON / OFF | Armed: a long paddle ON does not close CN9. A rinse gesture still rinses and consumes Armed. |
| **Last shot cooldown (min)** | 60 min | 5–240 min | After a blocked start, an Armed rinse, or a finished (non-rinse) shot, wait this long before the guard re-arms. Boot and scale reconnect re-arm immediately. |

Status shows `Off` / `Armed` / `Idle`.

## Example

BBW is on, the Bookoo is still in the drawer. A long paddle ON plays the
triple beep and leaves CN9 open. A short ON→OFF still rinses. After that,
the next long paddle starts a manual shot without a scale. When the scale
connects, the guard re-arms immediately.

Related: [Quick rinse](quick-rinse.md),
[Brew by weight](../features/brew-by-weight.md), [Alerts](../alerts.md)
(ATM / manual-no-scale).

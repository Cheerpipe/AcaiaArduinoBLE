# Tare

When the firmware sends tare to the scale, how long it waits for ~0 g, and
whether a late cup can trigger a second tare. Machine-level, under
**Settings → Machine and scale → Tare**, except **BBW protection**, which is
on the active brew preset.

Together with [Cup](cup.md), this is the setting side of
[Tare and retare](../features/tare-retare.md) and
[Cup protection](../features/cup-protection.md).

## When it applies

Automatic brew-by-weight shots. Timer-only and manual no-scale cycles skip
automatic retare and BBW protection. **Post-tare grace** is inactive when
**Automatic tare** is off.

First-drop / first-flow detection still runs against the tare zero even while
the grace window is open. A small stream of coffee does not wait for the
scale to settle. Placing the cup (including overshoot around 150–200 g) or a
finger tap is not first drop and does not block retare.

## Parameters

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Automatic tare** | ON | ON / OFF | Send an initial tare when an automatic shot starts. |
| **Post-tare grace (s)** | 2 s | 0.5–10 s | After a tare (start or late-cup retare), wait this long for ~0 g before using weight for **stop/control**. |
| **Automatic retare** | ON | ON / OFF | Allow one late-cup retare during the retare window. Fires on the cup-presence **placed** event. |
| **Retare window (s)** | 4 s | — | Time after shot start to detect and retare a late-placed cup. |
| **BBW protection (s)** | 12 s | minimum = retare window + 3 s | Inhibits automatic weight stop from shot start. First drops do not end this window. Skipped when brew by weight is off. |
| **Bookoo combined command** | ON | ON / OFF | Use the scale’s combined tare + start-timer command. Requires automatic tare. Bookoo only. |

## Example

Automatic tare is on, grace 2 s, retare window 4 s. The shot starts, the
scale tares, then you place the cup at 2 s. A second tare fires. Weight stop
still waits for BBW protection (default 12 s) so the cup weight cannot cut
the shot.

Related: [Tare and retare](../features/tare-retare.md),
[Cup protection](../features/cup-protection.md), [Cup](cup.md),
[Scales](scales.md).

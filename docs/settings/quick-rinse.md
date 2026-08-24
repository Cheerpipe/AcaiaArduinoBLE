# Quick rinse

A short paddle ON then OFF is a **rinse**, not a shot. Machine circuit stays closed for a
configured duration, then opens. Further paddle changes are ignored until the
rinse ends.

Quick rinse works in every [paddle mode](paddle.md). Paddle firmware shows
this group with **Paddle**. Momentary-switch firmware (with or without reed)
hides both and shows [Momentary](momentary.md) instead; it also hides the
Home rinse button. On momentary, a short press starts or stops a brew, and a
long press is mirrored to the machine (native rinse) without opening a
stopper rinse cycle.

## When it applies

From **Ready**, a paddle ON that is released within the **gesture** time
demotes the cycle to a rinse. Holding ON past that time keeps a brew (or, if
[No-scale BBW](no-scale-bbw.md) is Armed, may refuse to close the machine circuit).

A web quick rinse (when remote machine control is compiled in) follows the same rinse
duration and requires Admin unlock on that window. Without unlock, Home shows
the version footer instead of the Actions panel.

## Parameters

Machine-level, **Settings → Machine and scale**.

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Quick rinse gesture (s)** | 1 s | — | Maximum paddle ON time that still counts as a rinse when released. |
| **Quick rinse duration (s)** | 4 s | — | How long machine circuit stays closed after a quick rinse starts. |

Rinses are not stored in shot history (they are too short).

## Example

Default 1 s gesture / 4 s duration: flip the paddle ON and back OFF within a
second. The group head rinses for four seconds, then machine circuit opens.

Related: [Paddle](paddle.md), [Momentary](momentary.md), [No-scale BBW](no-scale-bbw.md),
[Shot history](../features/shot-history.md).

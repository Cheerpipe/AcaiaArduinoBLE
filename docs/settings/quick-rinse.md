# Quick rinse

A short paddle ON then OFF is a **rinse**, not a shot. CN9 stays closed for a
configured duration, then opens. Further paddle changes are ignored until the
rinse ends.

Quick rinse works in every [paddle mode](paddle.md).

## When it applies

From **Ready**, a paddle ON that is released within the **gesture** time
demotes the cycle to a rinse. Holding ON past that time keeps a brew (or, if
[No-scale BBW](no-scale-bbw.md) is Armed, may refuse to close CN9).

A web quick rinse (when remote CN9 is compiled in) follows the same rinse
duration.

## Parameters

Machine-level, **Settings → Machine and scale**.

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Quick rinse gesture (s)** | 1 s | — | Maximum paddle ON time that still counts as a rinse when released. |
| **Quick rinse duration (s)** | 4 s | — | How long CN9 stays closed after a quick rinse starts. |

Rinses are not stored in shot history (they are too short).

## Example

Default 1 s gesture / 4 s duration: flip the paddle ON and back OFF within a
second. The group head rinses for four seconds, then CN9 opens.

Related: [Paddle](paddle.md), [No-scale BBW](no-scale-bbw.md),
[Shot history](../features/shot-history.md).

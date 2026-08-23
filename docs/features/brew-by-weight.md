# Brew by weight

Automatic brew-by-weight (BBW) stops the shot when the scale reaches the
recipe target, minus a learned drip offset. It is **on by default**.

Turn it off if you want the paddle and time limits only. Tare and the shot
timer still run; weight stop, cup protection at start, automatic retare, and
offset learning do not.

## When it applies

BBW runs on an automatic shot that started with a usable scale. It does not
run on a rinse, a timer-only shot (BBW off), or a manual no-scale shot.

After the start-of-shot protection window ends (see
[Cup protection](cup-protection.md)), two fresh scale samples at or above
`target − learned offset` open the machine circuit. A short linear prediction can stop a
moment earlier. The learned offset is capped at 5.0 g and can be reset to a
baseline from the Web UI.

Every close path is also limited by **Max BBW time** and a firmware hard cap
of **60 seconds**.

## Parameters

These live on the **active preset** under **Settings → Brew**, except where
noted. **Home → Quick Settings** can toggle brew by weight for the session
(Manual).

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Brew by weight** | ON | ON / OFF | ON: stop by weight when a scale is usable. OFF: paddle, **Stop**, and time limits only. Fast, Slow, A→M, and No-scale BBW become read-only. |
| **Target (g)** | 36 g | 10–200 g | Goal weight. Stop aims at `target − learned offset`. |
| **Max BBW time (s)** | 50 s | 5–60 s | Operational time limit for a BBW cycle. Cannot exceed the hard 60 s cap. |
| **Baseline offset (g)** | 1.5 g | 0–5 g | Seed used by **Reset learned stop offset to baseline**. Save this before reset. |
| **Learned stop offset** | starts at 1.5 g | 0–5 g | Subtracted from the target (and from Fast/Slow recovery weights). Updated from post-drip weight after good shots. |

Fixed behavior (not separate settings):

- Direct stop: two fresh samples at the threshold after BBW protection ends.
- Predictive stop: may open the machine circuit slightly before the threshold.
- Scale loss: weight control pauses; paddle OFF and time limits stay in force.
  See [A→M time guard](auto-to-manual.md).

## Example

Double recipe at 36 g, learned offset 1.5 g. After the protection window, the
firmware treats about **34.5 g** as the cut point so post-drip weight lands
near 36 g.

Related: [Cup protection](cup-protection.md), [Tare](../settings/tare.md),
[Scales](../settings/scales.md), [No-scale BBW](../settings/no-scale-bbw.md).

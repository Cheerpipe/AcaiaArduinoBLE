# Slow extraction guard

Optional brew-by-weight enhancement, **on by default**. It is the inverse of
[Fast extraction guard](fast-extraction-guard.md): shots that have **not**
reached the target by a maximum brew time — often a grind that is too fine —
should not wait all the way to the machine circuit wall.

## When it applies

Automatic brew-by-weight with a usable scale. It does not run when BBW is
off, on rinses, or on manual no-scale shots.

BBW still wins by weight: if the target arrives on time, the cut is a normal
target stop. Fast extended and Slow extended cannot both own the same shot.

Elapsed time is measured from cycle start (machine circuit close). The learned stop
offset applies to the min recovery threshold (`min recovery − offset`).

Max brew time is a **decision point**, not a replacement for Max BBW time or
the hard 60 s cap.

## Parameters

Active preset, **Settings → Brew**. The ON/OFF switch is also on
**Home → Quick Settings** (read-only when brew by weight is off).

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Enable** | ON | ON / OFF | Master switch for the slow-shot recovery. |
| **Max brew time (s)** | 44 s | Must be greater than Fast’s min brew time when both are on | Latest time to wait for the normal BBW target. |
| **Min recovery weight (g)** | 34 g (Double and Single factory) | Same weight-cut tool as BBW | Floor if the shot must continue past max brew time. |

## How it works

1. **Normal stop** — the scale reaches the target at or before max brew time
   → machine circuit opens at the target (`normal_target`). Slow does not fire.
2. **Too slow** — max brew time is reached *without* the target:
   - Already at or above **min recovery** → cut now (`slow_max_time`).
   - Still below that floor → **extended** until min recovery
     (`slow_min_weight`) or until the machine circuit / Max BBW time wall.

## Example

Target 36 g, max brew time 44 s, min recovery 34 g. At 44 s the cup is only
at 20 g. If it were already over 34 g, the shot would cut there. Under the
floor, it may continue toward 34 g instead of waiting for 36 g or 60 s.

**Alerts → Slow extended pulse** can mark the extension when a local buzzer
is compiled in. See [Alerts](../alerts.md).

Related: [Brew by weight](brew-by-weight.md),
[Fast extraction guard](fast-extraction-guard.md).

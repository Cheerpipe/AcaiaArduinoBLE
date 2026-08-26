# Fast extraction guard

Optional brew-by-weight enhancement, **on by default**. It covers shots that
reach the target **too quickly** — often channeling or a grind that is too
coarse — where stopping immediately would yield a thin cup.

You still set a target weight. The guard adds a min BBW brew time and a
maximum recovery weight.

## When it applies

Automatic brew-by-weight with a usable scale. It does not run when BBW is
off, on rinses, or on manual no-scale shots.

Elapsed time is measured from cycle start (machine circuit close). The learned stop
offset applies to both the target and the max recovery weight.

Fast extended and Slow extended are mutually exclusive. If Fast already
extended the shot, Slow does not take over.

## Parameters

Active preset, **Settings → Brew**. The ON/OFF switch is also on
**Home → Quick Settings** (read-only when brew by weight is off).

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Enable** | ON | ON / OFF | Master switch for the extended-shot recovery. |
| **Min BBW brew time (s)** | 28 s | Must be less than Slow’s max BBW brew time when both are on | Normal BBW will not cut before this time. |
| **Max recovery weight (g)** | 42.5 g | Same weight-cut tool as BBW | Ceiling if the shot must continue past the target. |

When Fast is also on with Slow, factory Double uses a normal BBW window
between **28 s** and **44 s**.

## How it works

1. **Normal stop** — the scale reaches the target at or after the minimum brew
   time → machine circuit opens at the target (`normal_target`).
2. **Too fast** — the target arrives *before* the min BBW brew time → the
   shot enters **extended** mode until either:
   - **Max recovery weight** (`extended_max_weight`), or
   - **Min BBW brew time** is reached *and* the scale is still at least at
     target (`extended_min_time`).

## Example

Target 36 g, minimum time 28 s, max recovery 42.5 g. Coffee hits 36 g at
22 s. Instead of stopping thin, the shot continues toward 42.5 g or until
28 s have passed.

With a local buzzer compiled in, **Alerts → Extended shot pulse** can mark
the extension. See [Alerts](../alerts.md).

Related: [Brew by weight](brew-by-weight.md),
[Slow extraction guard](slow-extraction-guard.md).

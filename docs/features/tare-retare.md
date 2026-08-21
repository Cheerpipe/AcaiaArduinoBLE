# Tare and retare

Two automatic steps keep the scale at zero for brew-by-weight: an initial
tare when the shot starts, and a second tare if you put the cup down after
the paddle is already ON.

Both are **on by default**. You do not need to press tare on the scale.

## Automatic tare at start

When an automatic shot begins with a usable scale, the firmware sends tare
so the empty pan (or cup already on the scale) reads ~0 g. After that,
**Post-tare grace** waits for the reading to settle before weight is used
to stop or control the shot. First-drop detection still runs against that
tare zero.

## Automatic late-cup retare

If the cup is not on the scale yet, you can place it after the shot has
started. Cup-presence detection watches for a stable load above the minimum
cup weight. When a cup is **placed** inside the **retare window** (default
**4 s**), the firmware tares again once—automatically—without restarting
the shot timer.

Putting the cup down (including a short overshoot around 150–200 g) or a
finger tap is not treated as first drop and does not block retare. After
the late tare, post-tare grace runs again so the empty-cup weight cannot
cut the shot; [Cup protection](cup-protection.md) still blocks weight stop
for the full BBW protection window.

## When it applies

Automatic brew-by-weight shots. Timer-only (BBW off) and manual no-scale
cycles skip automatic retare (and BBW protection). Post-tare grace is
inactive when **Automatic tare** is off.

## Parameters

Configured under **Settings → Machine and scale → Tare** (and **Cup** for
presence thresholds). Full tables: [Tare](../settings/tare.md),
[Cup](../settings/cup.md).

| Setting | Default | Effect |
| --- | --- | --- |
| **Automatic tare** | ON | Initial tare when an automatic shot starts. |
| **Automatic retare** | ON | One late-cup retare on the cup **placed** event. |
| **Retare window (s)** | 4 s | Time after shot start to accept a late cup. |
| **Post-tare grace (s)** | 2 s | Settle wait after start tare or late-cup retare. |

## Example

Cup already on the scale: shot starts → one automatic tare → brew continues.

Cup placed two seconds after paddle ON: start tare runs, then cup detection
fires a second automatic tare inside the 4 s window. The shot timer does
not restart; weight stop stays blocked until BBW protection ends.

Related: [Cup protection](cup-protection.md), [Brew by weight](brew-by-weight.md),
[Tare](../settings/tare.md), [Cup](../settings/cup.md).

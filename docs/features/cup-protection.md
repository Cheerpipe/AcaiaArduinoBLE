# Cup protection

Friendly name for the start-of-shot defenses that keep a late cup, a finger
on the pan, or a bump from stopping the extraction.

In the Web UI the same behavior is split across **BBW protection**,
**Automatic retare**, **Cup**, and **Tare**. This page explains how they work
together. Parameter tables for cup detection and tare are on those setting
pages.

## When it applies

Only on automatic brew-by-weight shots. Timer-only (BBW off) and manual
no-scale cycles skip retare and BBW protection.

From paddle ON, three things run in parallel:

1. **BBW protection (pre-arm)** — automatic weight stop is blocked for a
   configurable window (default **12 s**). First drops can still beep and log;
   they do not end this window.
2. **Automatic retare** — if a cup is placed during the retare window
   (default **4 s**), the firmware tares again without restarting the shot
   timer.
3. **Cup presence** — a stable load above the minimum cup weight counts as
   placed; a confirmed drop to the removed threshold counts as lifted.

Weight samples that look like noise or an implausible spike stay visible as
observed weight but do not enter stop logic or offset learning.

Do **not** press the scale pan with a finger to “cancel” a shot. Protection
and retare may ignore that weight. Use the paddle (see
[Paddle](../settings/paddle.md)).

## Parameters

BBW protection is a brew setting on the active preset. Retare and settle time
are under **Settings → Machine and scale → Tare**. Presence thresholds are
under **Cup**.

| Setting | Where | Default | Range | Effect on the shot |
| --- | --- | --- | --- | --- |
| **BBW protection (s)** | Brew | 12 s | minimum = retare window + 3 s | Inhibits automatic weight stop from shot start. First drops do not end it. Skipped when BBW is off. |
| **Automatic retare** | Tare | ON | ON / OFF | One late-cup retare during the retare window, on the cup **placed** event. |
| **Retare window (s)** | Tare | 4 s | — | Time after shot start to accept a late cup. |
| **Automatic tare** | Tare | ON | ON / OFF | Initial tare when an automatic shot starts. Post-tare grace is inactive when this is off. |
| **Post-tare grace (s)** | Tare | 2 s | 0.5–10 s | After a tare, wait for ~0 g before using weight for **stop/control**. First-drop detection still runs against the tare zero. A cup landing or a finger tap is not first drop. |
| **Minimum cup weight (g)** | Cup | 10 g | — | Stable load that counts as a cup placed. |
| **Cup-removed threshold (g)** | Cup | −3 g | — | Confirmed weight at or below this means the cup was lifted. |

Cup placement stability (samples, tolerance, gap, min stable time) is
documented in [Cup](../settings/cup.md).

## Example

You flip the paddle and set the cup down two seconds later. Retare fires
inside the 4 s window. Weight stop stays blocked until BBW protection ends
(~12 s), so the empty-cup or finger weight cannot cut the shot. After that
window, brew-by-weight arms normally.

Related: [Brew by weight](brew-by-weight.md),
[Tare and retare](tare-retare.md), [Cup](../settings/cup.md),
[Tare](../settings/tare.md).

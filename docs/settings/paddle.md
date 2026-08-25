# Paddle

How the physical **latch** brew switch starts, holds, and ends a shot. Choose
the mode in **Settings → Machine and scale → Paddle** (`paddleMode`). It is
**machine-level**, not per-preset. Selector order is **Auto**, **Natural**,
**Original**. Default is **Natural**.

This page applies to firmware compiled as `SHOT_STOPPER_MACHINE_TYPE=0`
(paddle / latch). That firmware shows **Paddle**. **Quick rinse** is shared
with momentary (Enable rinse defaults off on both). Momentary builds hide
**Paddle** and show [Momentary](momentary.md) instead.

This is the main day-to-day choice. All modes still share quick rinse, machine circuit
safety limits, and physical-paddle priority. They differ in whether releasing
the paddle mid-shot ends the brew or leaves brew-by-weight in control.

The physical paddle always has priority over Web or remote commands.

## When it applies

ON always starts an extraction (or a rinse if you release inside the gesture
window). The special “keep machine circuit closed after OFF” rules apply only to
**automatic brew-by-weight with a usable scale**. Without a scale, or with
brew by weight off, paddle OFF ends the shot like Natural in every mode
(Auto, Natural, Original), and **Max BBW time does not apply** — only the
firmware **60 s** circuit cap.

## How K1 follows the paddle

K1 **mirrors ON, not OFF**. While the paddle is ON and start is allowed,
firmware keeps rewriting the closed level so a BLE/coex glitch cannot leave
software CLOSED and the contact open. Paddle OFF does **not** open K1 by
itself — the stopper requests stop, and the paddle specialization opens.

That drive model is the same with or without a scale. The scale only changes
whether brew-by-weight may walk away after OFF.

Exceptions:

1. **Automatic cut** (weight, walls, A→M, cup removed while ON) — the stopper
   requests stop and K1 opens. If the paddle is still ON: stopper
   `REQUIRES_OFF`, paddle-return reminder, K1 stays open until a stable OFF.
   Firmware does not re-close on that leftover hold.
2. **Start inhibit** (no-scale BBW armed, require-cup) — ON is not forwarded.
   A blocked hold does not close K1; release and go ON again.
3. **Rinse** — K1 stays closed for the rinse duration with the paddle OFF.
4. **Original / Auto walk-away** (BBW + usable scale only) — K1 stays closed
   after paddle OFF until automatic cut. Natural, no scale, or BBW off: OFF
   opens K1.

A Web/remote start with the paddle OFF leaves K1 closed; the ON-only mirror
does not open it.

## Parameters

| Setting | Default | Values | Effect on the shot |
| --- | --- | --- | --- |
| **Paddle mode** | Natural | Auto / Natural / Original | See the table below. |

| | **Auto** | **Natural** (default) | **Original** |
| --- | --- | --- | --- |
| **Feel** | Automatic BBW finish; paddle ON/OFF switches subtype | Paddle = normal brew switch | Legacy Shot Stopper start gesture |
| **ON** | Starts the extraction (auto-natural) | Starts the extraction | Starts the extraction |
| **OFF after rinse window** | Keeps machine circuit closed (auto-original) on automatic BBW+scale shots | Ends the shot and opens the machine circuit | Keeps machine circuit closed on automatic BBW+scale shots; stopper finishes by weight |
| **Early manual cut** | No paddle cut on BBW+scale (Stop remote / walls still apply). ON↔OFF only switches subtype | Move paddle **OFF** | Move paddle **ON** (promotes that shot to Natural), then **OFF** to cut |

## Natural

1. Move the paddle **ON** → machine circuit closes and the shot starts.
2. Leave it **ON** → the stopper finishes by weight when BBW and the scale
   are available, or keep holding for a manual/no-scale cycle.
3. Move it **OFF** at any time after the rinse gesture window → the shot ends
   and machine circuit opens immediately.

Short ON→OFF within the [quick rinse](quick-rinse.md) gesture still demotes
to a rinse.

## Original

Legacy Shot Stopper workflow for automatic brew-by-weight:

1. Move the paddle **ON** to start.
2. After about 2–3 s (past the rinse gesture), move it **OFF**. The stopper
   **keeps the extraction running** and ends it by weight.
3. While the paddle stays **ON** during that automatic BBW shot, weight stop
   is held off and Max BBW time does not cut yet; the absolute **60 s** machine circuit
   hard cap still applies.
4. To stop early: move the paddle **ON** during the shot (that shot is
   promoted to Natural semantics), then **OFF** when you want to cut.
5. A short ON→OFF inside the rinse gesture is still a quick rinse.

Without a usable scale, or with brew by weight off, paddle OFF ends the shot
like Natural. After an automatic weight stop with the paddle still ON, the
paddle-return reminder still asks you to return the paddle to OFF.

## Auto

Automatic brew-by-weight finish **whether the paddle stays ON or OFF**. In
one Auto shot the paddle only switches subtype (the saved mode stays Auto):

1. Move the paddle **ON** to start. While it stays ON the shot is
   **auto-natural**: Natural rules except OFF after the rinse window does
   **not** end the shot. Weight / A→M / Max BBW time can still cut with the
   paddle ON.
2. Move it **OFF** after the rinse gesture → **auto-original**: machine circuit stays
   closed until automatic stop.
3. Move it **ON** again → back to auto-natural. You can switch back and forth
   in the same shot.
4. A short ON→OFF inside the rinse gesture is still a quick rinse.

Unlike Original, Auto does **not** hold off weight stop while the paddle is
ON, and ON does not promote to a one-way Natural cut on the next OFF.

## Example

Default **Natural**: brew like a stock Micra. Flip ON, walk away, the
controller stops by weight; flip OFF any time after the rinse window to cut
now.

Related: [Quick rinse](quick-rinse.md), [Momentary](momentary.md),
[Brew by weight](../features/brew-by-weight.md), [Alerts](../alerts.md)
(paddle-off reminder).

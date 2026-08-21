# Paddle

How the physical brew switch starts, holds, and ends a shot. Choose the mode
in **Settings → Machine and scale → Paddle** (`paddleMode`). It is
**machine-level**, not per-preset. Selector order is **Auto**, **Natural**,
**Original**. Default is **Natural**.

This is the main day-to-day choice. All modes still share quick rinse, CN9
safety limits, and physical-paddle priority. They differ in whether releasing
the paddle mid-shot ends the brew or leaves brew-by-weight in control.

The physical paddle always has priority over Web or remote commands.

## When it applies

ON always starts an extraction (or a rinse if you release inside the gesture
window). The special “keep CN9 closed after OFF” rules apply only to
**automatic brew-by-weight with a usable scale**. Without a scale, or with
brew by weight off, paddle OFF ends the shot like Natural in every mode.

## Parameters

| Setting | Default | Values | Effect on the shot |
| --- | --- | --- | --- |
| **Paddle mode** | Natural | Auto / Natural / Original | See the table below. |

| | **Auto** | **Natural** (default) | **Original** |
| --- | --- | --- | --- |
| **Feel** | Automatic BBW finish; paddle ON/OFF switches subtype | Paddle = normal brew switch | Legacy Shot Stopper start gesture |
| **ON** | Starts the extraction (auto-natural) | Starts the extraction | Starts the extraction |
| **OFF after rinse window** | Keeps CN9 closed (auto-original) on automatic BBW+scale shots | Ends the shot and opens CN9 | Keeps CN9 closed on automatic BBW+scale shots; stopper finishes by weight |
| **Early manual cut** | No paddle cut on BBW+scale (Stop remote / walls still apply). ON↔OFF only switches subtype | Move paddle **OFF** | Move paddle **ON** (promotes that shot to Natural), then **OFF** to cut |

## Natural

1. Move the paddle **ON** → CN9 closes and the shot starts.
2. Leave it **ON** → the stopper finishes by weight when BBW and the scale
   are available, or keep holding for a manual/no-scale cycle.
3. Move it **OFF** at any time after the rinse gesture window → the shot ends
   and CN9 opens immediately.

Short ON→OFF within the [quick rinse](quick-rinse.md) gesture still demotes
to a rinse.

## Original

Legacy Shot Stopper workflow for automatic brew-by-weight:

1. Move the paddle **ON** to start.
2. After about 2–3 s (past the rinse gesture), move it **OFF**. The stopper
   **keeps the extraction running** and ends it by weight.
3. While the paddle stays **ON** during that automatic BBW shot, weight stop
   is held off and Max BBW time does not cut yet; the absolute **60 s** CN9
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
2. Move it **OFF** after the rinse gesture → **auto-original**: CN9 stays
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

Related: [Quick rinse](quick-rinse.md),
[Brew by weight](../features/brew-by-weight.md), [Alerts](../alerts.md)
(paddle-off reminder).

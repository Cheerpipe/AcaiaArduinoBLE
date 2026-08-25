# Quick rinse

A firmware **rinse** keeps the group on for a configured duration, then turns
it off. It is not a shot: no history, no last-shot overwrite, no A→M samples.

Detection is **machine-owned**. Duration is **rinse-owned** and starts when the
stopper accepts the rinse, not from a raw GPIO timestamp.

**Settings → Machine and scale → Quick rinse** is shown on paddle, momentary,
and reed firmware. Home **Start rinse** follows the same Enable quick rinse flag.

## When it applies

**Enable quick rinse** must be on. Default is **off** on every machine type. With it
off, paddle short ON→OFF is a shot, and a momentary long-press is native 1:1
(no `RINSE` cycle).

- **Paddle:** from Ready or during a brew, ON then OFF within the **gesture**
  time is a rinse. Holding ON past that time keeps a brew (or, if
  [No-scale BBW](no-scale-bbw.md) is Armed, may refuse to close the machine
  circuit).
- **Momentary / reed:** a long-press that **starts from idle** (off / confirmed
  off) and reaches the gesture time is a rinse. In **Button press** the shot
  starts on press and demotes to rinse at the threshold. In **Button release**
  rinse starts directly (never a shot). A long-press during assumed/confirmed
  on of an existing shot is not a rinse.

A web rinse (when remote machine control is compiled in) uses the same duration
and requires Admin unlock. Without unlock, Home shows the version footer
instead of the Actions panel. Web rinse is refused when Enable quick rinse is off.

## Parameters

Machine-level, **Settings → Machine and scale → Quick rinse**.

| Setting | Default | Range | Effect |
| --- | --- | --- | --- |
| **Enable quick rinse** | Off | ON / OFF | Firmware rinse on/off. |
| **Rinse gesture (s)** | 1 s | 0.1–5 s | Paddle: how long you can leave the paddle ON and still get a rinse when you flip it OFF. Momentary: how long to hold the switch from idle before a rinse starts. |
| **Rinse duration (s)** | 4 s | 0.5–10 s | How long water runs through the group after a rinse starts. |

Rinses are not stored in shot history (they are too short).

## Example

Default 1 s gesture / 4 s duration, with **Enable quick rinse** on. On paddle, flip
ON and back OFF within a second: the group rinses for four seconds, then
opens. On momentary, hold the switch for at least a second from idle:
firmware pulses start, keeps the group on for four seconds, then pulses stop.

Related: [Paddle](paddle.md), [Momentary](momentary.md), [No-scale BBW](no-scale-bbw.md),
[Shot history](../features/shot-history.md).

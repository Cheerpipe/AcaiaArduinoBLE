# Alerts

Sounds and the optional onboard LED that tell you what the controller is
doing: tare, first drops, paddle left ON, scale lost or connected, and
extended-shot pulses.

This page covers both the feature and the **Settings → Alerts** group. All of
these settings are **machine-level** (not per-preset).

## When it applies

**Sound alerts** is the master switch (default ON). Off silences operational
alerts on both the local buzzer and the scale; the individual checkboxes are
kept.

Sounds are event-first: tare/start/stop, first drop, paddle reminder,
completion extra, and the triple alerts follow **Output channel** when a
local buzzer is compiled in (`SHOT_STOPPER_ENABLE_BUZZER`). `=1` plays
RTTTL melodies on a passive piezo; `=2` plays on/off beeps on an active
buzzer. Channel selection is the same for both.

Without buzzer support, Output channel and the triple checkboxes are hidden.
The default channel is then **Scale only**.

Shot completion still adds one extra beep after stop (not configurable).

## Parameters

| Setting | Default | Range / notes | Effect |
| --- | --- | --- | --- |
| **Sound alerts** | ON | ON / OFF | Master switch in **Settings → Alerts**. |
| **Output channel** | **Buzzer only** with a local buzzer; **Scale only** without | Buzzer only / Scale only / Scale priority | Where each alert plays. **Buzzer only**: all sound on the local buzzer. **Scale only**: scale path; scale-incapable triples are muted. **Scale priority**: scale when connected/able, else buzzer; never both for one event. Shown only with buzzer support. |
| **Beep when coffee starts** | ON | ON / OFF | One beep on first coffee drops during an automatic shot. Ignored when brew by weight is off. |
| **Paddle-off reminder** | ON | ON / OFF | Repeat beeps while the **physical paddle stays ON** and **CN9 is already open**. |
| **Paddle reminder interval (s)** | 10 s | 5–60 s | Time between reminder beeps. |
| **Paddle reminder limit (min)** | 15 min | 1–60 min | Stop beeping after this time even if the paddle remains ON. |
| **Scale lost** | (buzzer builds) | ON / OFF | Echo inverted on the local buzzer when the scale disconnects (idle or during a shot). Hidden / disabled when Output channel is Scale only. |
| **ATM / manual-no-scale** | (buzzer builds) | ON / OFF | Triple beeps on the local buzzer when A→M ends or when BBW needs a scale that is missing. Disabled when Output channel is Scale only. |
| **Scale connected** | ON (Buzzer only or Scale priority) | ON / OFF | Distinctive echo when a scale connects or reconnects. Always the local buzzer. |
| **Blue LED while scale connected** | ON | ON / OFF | Onboard GPIO 1 HIGH while a scale is BLE-connected. Independent of Sound alerts. |
| **Extended shot pulse** | Fast | Disabled / Slow / Medium / Fast / Rapid | Local-buzzer pulses while Fast extraction guard keeps the shot running past the normal cut. Disabled when Output channel is Scale only. |
| **Slow extended pulse** | Fast | Same rates | Same idea for Slow extraction guard. |

In **Buzzer only** (and Scale priority when the scale is not usable),
tare/start/stop sounds follow CN9/paddle/retare immediately and do not wait
for a Bluetooth round-trip.

**Mute scale in Buzzer only** and **Scale volume** live under
[Scales](settings/scales.md). They change scale speaker behavior, not this
Alerts group.

## Example

Brew by weight finishes, CN9 opens, but you left the paddle ON. The
paddle-off reminder repeats every 10 s for up to 15 minutes until you return
the paddle to OFF.

Related: [A→M time guard](features/auto-to-manual.md),
[Fast extraction guard](features/fast-extraction-guard.md),
[Slow extraction guard](features/slow-extraction-guard.md),
[Hardware](HARDWARE.md) (buzzer GPIO and LED).

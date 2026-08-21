# A→M time guard

Safety layer for automatic brew-by-weight shots that **lose the scale**
mid-extraction. **On by default.**

A→M means auto-to-manual: weight stop is suspended, but the shot is still
running. The guard closes CN9 on a shorter deadline so a silent fallback does
not become an over-extracted shot.

## When it applies

Automatic brew-by-weight shots only. It does **not** apply to shots that
start as manual (`MANUAL_NO_SCALE`), timer-only (BBW off), or rinses.

When BLE drops or the stream goes stale, weight control pauses. The firmware
keeps trying to reconnect for the **whole** cycle. If the scale returns with
three coherent samples, weight stop resumes (including Fast or Slow, if
enabled) and A→M enforcement clears. A later disconnect in the same cycle
reuses the same deadline counted from cycle start.

There is no permanent lockout to manual after a fixed reconnect window.
Reconnect stays preferred.

## Parameters

Active preset. The ON/OFF switch is also on **Home → Quick Settings**
(read-only when brew by weight is off).

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Enable A→M time guard** | ON | ON / OFF | Master switch. OFF: if the scale never returns, the shot runs until paddle OFF or the CN9 / 60 s wall. |
| **Limit mode** | Auto | Auto / Manual | **Auto** uses a trend of the last five good shot durations. **Manual** uses a fixed number of seconds. |
| **Manual limit (s)** | 32 s | 10 s … Max BBW time | Used when Limit mode is Manual. |
| **Trend (s)** | ~32 s | Read-only | Current Auto prediction. Always shown. |
| **Baseline duration (s)** | 32 s | — | Seed for **Reset A→M samples** (five equal values). Also the factory default for Manual limit. |

Deadline = cycle start + limit. The limit is computed once when an automatic
brew is confirmed. Enforcement starts on the first scale-loss suspend after
arming.

The live panel shows `Off` / `Idle` / `Armed` / `A→M · Ns`. History records
`stop_detail = auto_to_manual` and `cut_type = limit`.

## Duration samples

Successful shots feed a ring of five durations, whether the guard is on or
not:

- Total shot duration; error ≤ 10 % vs goal; not Fast/Slow extended; not a
  rinse; not stopped by this guard; post-drip weight available.
- Auto and manual shots can qualify when weight/error criteria are met.
- A fresh device starts with five logical **32 s** samples.

## Example

An automatic Double loses Bluetooth at 12 s. Trend limit is 32 s. The
firmware keeps reconnecting, but if the scale is still gone at 32 s from
start, CN9 opens. If the scale had come back at 20 s with three good samples,
weight stop would have resumed and the deadline would have been cleared.

Related: [Brew by weight](brew-by-weight.md), [Alerts](../alerts.md)
(ATM / manual-no-scale triple beeps).

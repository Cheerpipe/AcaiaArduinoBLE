# Shot history

Automatic brew-by-weight shots with a connected scale are stored on the
controller and shown in the Web UI shot log. Use it to see why a shot ended
and how close it landed to the target.

## What is recorded

Only **automatic BBW** shots qualify: scale present at start, brew-by-weight
enabled (not timer-only), and automatic weight control active. Manual shots,
timer-only brews, and cycles without a scale are **not** stored.

Typical fields include local time (from the configured timezone offset),
duration, goal and actual weight, error, average flow, first-drop time,
whether Fast/Slow guards ran or extended the shot, `shot_type`, `cut_type`
(`auto`, `manual`, `limit`), and `stop_detail` (for example
`normal_target`, `paddle`, `web_stop`, `wall_limit`, `hard_limit`,
`extended_max_weight`, `cup_removed`).

The log holds up to **120** shots. The following are never stored:

- Quick rinses and cycles shorter than 10 s
- Manual, timer-only, or no-scale shots
- Shots whose final weight is missing or below 1 g (e.g. scale off the
  machine or disconnected)

Curve samples and the history record are written **once** when the cycle
closes (after the configured drip delay), not during an active brew. The
live `shotCurve` on the status API is in-memory only for the home UI.

## In the Web UI

Open the shot history table to browse rows, delete one entry, clear the
whole log, or export CSV. Clearing requires an explicit confirm.

The table loads **10 shots at a time** as you scroll (newest first). The
20 s poll refreshes only the first page so new shots appear at the top
without re-downloading the whole log. Export CSV fetches the full log in
one request.

History averages (duration, weight, error, flow) use only **auto** shots
with actual weight at least 1 g from the last 10 stored entries.

USB: `CLEAR_SHOTS` (see [USB serial CLI](../SERIAL_CLI.md)).

Related: [Brew by weight](brew-by-weight.md), [FAQ](../FAQ.md),
[Wi-Fi](../settings/wifi.md) (timezone for wall-clock labels).

# Shot history

Every finished brew (not a rinse) is stored on the controller and shown in
the Web UI shot log. Use it to see why a shot ended and how close it landed
to the target.

## What is recorded

Typical fields include local time (from the configured timezone offset),
duration, goal and actual weight, error, average flow, first-drop time,
whether Fast/Slow guards ran or extended the shot, `shot_type`, `cut_type`
(`auto`, `manual`, `limit`), and `stop_detail` (for example
`normal_target`, `paddle`, `web_stop`, `wall_limit`, `hard_limit`,
`extended_max_weight`, `cup_removed`).

The log holds up to **120** shots. Quick rinses are not stored.

## In the Web UI

Open the shot history table to browse rows, delete one entry, clear the
whole log, or export CSV. Clearing requires an explicit confirm.

The table loads **10 shots at a time** as you scroll (newest first). The
20 s poll refreshes only the first page so new shots appear at the top
without re-downloading the whole log. Export CSV fetches the full log in
one request.

USB: `CLEAR_SHOTS` (see [USB serial CLI](../SERIAL_CLI.md)).

Related: [Brew by weight](brew-by-weight.md), [FAQ](../FAQ.md),
[Wi-Fi](../settings/wifi.md) (timezone for wall-clock labels).

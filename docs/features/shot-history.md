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
(`auto`, `manual`, `limit`), `stop_detail` (for example
`normal_target`, `paddle`, `web_stop`, `wall_limit`, `hard_limit`,
`extended_max_weight`, `cup_removed`), and a manual `rating` from 0
(unrated) to 5. Rate a finished shot from Home (Last/Current shot) or from
a history card by tapping a star; tapping the current star again clears the
score.

The log holds up to **120** shots. The following are never stored:

- Quick rinses and cycles shorter than 10 s
- Manual, timer-only, or no-scale shots
- Shots whose final weight is missing or below 1 g (e.g. scale off the
  machine or disconnected)

Those empty or sub-1 g shots are still shown on Home as the **last shot**
(status always reflects the current and last finished cycle). They are not
written to history, not used in averages, and not used for learned stop
offset or A→M samples.

Curve samples and the history record are written **once** when the cycle
closes (after the configured drip delay), not during an active brew. The
live `shotCurve` on the status API is in-memory only for the home UI.

## In the Web UI

Open the shot history table to browse rows, delete one entry, clear the
whole log, or export CSV. Clearing requires an explicit confirm.

Sort the list by **Date** or **Rating**, ascending or descending. Date
defaults to newest first. Rating puts unrated shots (0 stars) at the end
in both directions; equal scores keep newer shots first. The table loads
**10 shots at a time** as you scroll. The 20 s poll refreshes only the
first page of the current sort so new shots appear without re-downloading
the whole log. Export CSV fetches the full log newest-first in one
request, independent of the on-screen sort.

History averages (duration, weight, error, flow) use only **auto** shots
with actual weight at least 1 g from the last 10 stored entries, even
when the list is sorted by rating or oldest-first.

USB: `CLEAR_SHOTS` (see [USB serial CLI](../SERIAL_CLI.md)).

Related: [Brew by weight](brew-by-weight.md), [FAQ](../FAQ.md),
[Wi-Fi](../settings/wifi.md) (timezone for wall-clock labels).

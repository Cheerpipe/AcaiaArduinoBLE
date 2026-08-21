# Cup

Thresholds that decide whether a cup is **on the scale** or **lifted**. Used
by late-cup retare and start-of-shot protection. Machine-level, under
**Settings → Machine and scale → Cup**.

How those detections protect the shot is explained in
[Cup protection](../features/cup-protection.md).

## When it applies

Automatic brew-by-weight. A stable load at or above the minimum cup weight
counts as **placed**. A confirmed weight at or below the removed threshold
counts as **lifted**. Placement also requires a short run of stable samples.

## Parameters

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Minimum cup weight (g)** | 10 g | 1–500 g | Stable load that counts as a cup being placed. First-flow and retare use this same value (not a hardcoded 10 g or 150 g). |
| **Cup-removed threshold (g)** | −3 g | — | Confirmed weight at or below this means the cup was lifted. |
| **Placement samples** | 3 | — | Number of stable samples required before cup-present. |
| **Placement tolerance (g)** | 2.0 g | — | How much those samples may differ. |
| **Max sample gap (s)** | 0.5 s | — | Maximum gap between those samples. |
| **Min stable time (s)** | 0.3 s | — | Minimum time the load must stay stable. |

## Example

You set a cup down during the retare window. After three samples within 2 g
of each other, lasting at least 0.3 s, the firmware treats it as placed and
can retare. Lifting the cup until the scale reads −3 g or below counts as
removed.

Related: [Cup protection](../features/cup-protection.md),
[Tare and retare](../features/tare-retare.md),
[Tare](tare.md).

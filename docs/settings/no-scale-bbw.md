# No-scale BBW

Machine-level protection for attempts to use Brew by weight without a usable
scale. Configure it under **Settings → Machine and scale → No-scale BBW**.

It applies only while **Brew by weight** is on. With BBW off, shots and rinses
remain manual regardless of this setting. A scale is usable only when its link
is available and its weight stream is fresh.

## Modes

| Mode | Shot without a scale | Rinse without a scale | Cooldown |
| --- | --- | --- | --- |
| **Allow manual brewing** | Allowed | Allowed | Not used |
| **Warn once, then allow** (default) | First attempt is blocked and alerts; later attempts are manual | An Armed rinse runs and consumes the warning | Protection returns after the configured delay |
| **Require a scale** | Always blocked | Always blocked | Not used; protection stays armed |

In **Require a scale**, the machine circuit remains open: paddle, momentary
switch, Web start, and Web rinse cannot drive the relay until the scale becomes
usable or BBW is turned off. If the scale reconnects while an activator is held,
release it before starting again; reconnection alone never closes the circuit.

**Protection returns after (min)** is visible only for **Warn once, then
allow**. It defaults to 60 minutes and accepts 5–240 minutes. Boot and scale
reconnect also re-arm that mode immediately.

Home shows the configured mode as a read-only summary. Its live status is
**Off**, **Armed**, **Temporarily allowed** (with remaining time), **Scale
required**, or **Ready** when Require scale is configured and the scale is
usable.

The optional **Manual without scale (BBW on)** alert sounds once per distinct
blocked attempt; holding the activator does not repeat it continuously.

Related: [Quick rinse](quick-rinse.md), [Momentary](momentary.md),
[Brew by weight](../features/brew-by-weight.md), [Alerts](../alerts.md).

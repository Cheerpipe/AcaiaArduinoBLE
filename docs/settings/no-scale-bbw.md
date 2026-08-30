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
| **Require a scale** | Blocked unless temporarily allowed with the physical gesture | Blocked unless temporarily allowed with the physical gesture | Protection returns after the configured delay |

In **Require a scale**, shots and rinses cannot start until the scale is usable.
If the scale is unavailable and you need to brew, start with the activator OFF,
then switch it ON and OFF three times within two seconds. The scale-connected
melody confirms that manual brewing is temporarily available. A fresh activation
is required after the gesture, so its final movement never starts a shot. If the
scale reconnects while the activator is held, release it before starting again.

**Protection returns after (min)** applies to **Warn once, then allow** and the
temporary **Require a scale** override. It defaults to 60 minutes and accepts
5–240 minutes. Boot and scale reconnect re-arm protection immediately.

Home shows the configured mode as a read-only summary. Its live status is
**Off**, **Armed**, **Temporarily allowed** (with remaining time), **Scale
required**, or **Ready** when Require scale is configured and the scale is
usable.

The optional **Manual without scale (BBW on)** alert sounds once per distinct
blocked attempt; holding the activator does not repeat it continuously.

Related: [Quick rinse](quick-rinse.md), [Momentary](momentary.md),
[Brew by weight](../features/brew-by-weight.md), [Alerts](../alerts.md).

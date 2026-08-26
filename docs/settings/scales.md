# Scales

Which Bluetooth scale to use, how long to wait after a shot for drip, and
Bookoo-specific volume / combined tare. Machine-level, under
**Settings → Machine and scale → Scales**.

Daily defaults assume a **Bookoo** Themis Mini or Ultra. **Acaia**,
**Felicita**, **AtomHeart Eclair**, **Decent**, **DiFluid Microbalance**,
**MyScale**, **Varia AKU**, **Eureka Precisa** (named GAP), and **WeighMyBru**
are also supported through the vendored EspressoScaleBLE library. Compatibility
details:
[Scale compatibility](../../libraries/EspressoScaleBLE/README.md#scale-compatibility).

Timemore Black Mirror DUO, Timemore Dot, Acaia Umbra, and Eureka units that
advertise no GAP name are not supported.

## When it applies

A usable scale is required for automatic brew-by-weight. If the scale is
missing, see [No-scale BBW](no-scale-bbw.md). If it drops mid-shot, see
[A→M time guard](../features/auto-to-manual.md).

**Drip delay** runs after machine circuit opens. It is used for Last Shot, history,
offset learning, and eligible A→M samples.

## Parameters

| Setting | Default | Range / notes | Effect |
| --- | --- | --- | --- |
| **Always use this scale** | ON (`full` MAC cache) | ON / OFF | ON: name-scan for compatible scales; when a preferred MAC is set, only that MAC is connected; other compatible advertisements are stored in history (up to 8) without connecting. OFF: connect the first compatible scale; do not lock preferred. |
| **Preferred scale** | none until you pick one | Dropdown of BLE-seen scales | Which MAC to lock. **Clear preferred** pauses discovery 30 s and keeps history. |
| **Drip delay (s)** | 3.0 s | 0–10 s | Wait after a shot ends before capturing the final post-drip weight. `0` finalizes on the next control loop with no intentional window. |
| **Timer stop extra delay (ms)** | 0 ms | 0–1000 ms | Pad after the scale timer catches up to circuit whole seconds, before `STOP_TIMER`. `0` stops in that same instant. Does not delay the local machine circuit beep. |
| **Bookoo combined command** | ON | ON / OFF | Combined tare + start-timer. Requires automatic tare. Also listed under [Tare](tare.md). |
| **Mute scale in Buzzer only** | ON | ON / OFF | Bookoo/generic: send silence (volume 0) on connect/reconnect, when Output channel is saved as Buzzer only, and when this option is turned on. Applies only in **Buzzer only**. |
| **Scale volume** | 4 | 1–5 or Disabled | Bookoo/generic: set on connect/reconnect. Applies only in **Scale only** and **Scale priority**. |
| **AtomHeart Eclair** | informational | — | Uses normal tare/timer commands. No configurable volume, beep, mode, combined command, or documented command sound. In Buzzer only and Scale priority, alerts use the local buzzer; Scale only omits unsupported sounds. |

If the scale disconnects or **notifications go silent** during an automatic
extraction, weight control is suspended. Rejected brew samples (post-tare,
slew) and a stable accepted weight do not count as a lost scale. Recovery
needs three coherent samples on the current connection. Paddle OFF and time
limits remain in force.

## Example

Always use this scale is on. You pick your Bookoo in **Preferred scale**.
Other scales nearby are remembered in history but not connected. After each
shot, the firmware waits 3 s of drip before storing the final weight used
for offset learning.

Related: [Brew by weight](../features/brew-by-weight.md), [Tare](tare.md),
[Alerts](../alerts.md).

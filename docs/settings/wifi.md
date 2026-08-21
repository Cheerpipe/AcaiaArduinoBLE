# Wi-Fi

How the controller joins your home network (STA) and when it falls back to
its own access point. Details for the fallback AP itself are in [AP](ap.md).

The Web UI is reachable from any client on the same network. There is no
separate Web UI login beyond the sign-in password used for configuration.
Use a trusted network.

## When it applies

Wi-Fi and the HTTP server start regardless of paddle position.
**Configuration changes** still need a maintenance window: paddle OFF, CN9
open, no active cycle.

You cannot change workflow settings during an active shot. Read-only status
stays available.

## Parameters and behavior

| Setting / behavior | Default | Effect |
| --- | --- | --- |
| **Home Wi-Fi (STA)** | none on a fresh flash | Saved SSID/password. Device joins your network and serves the Web UI at the STA IP. |
| **IP mode** | DHCP | **DHCP** or **static** (`ip` / `netmask` / `gateway` / `dns1` / `dns2`). |
| **Confirm window** | 3 minutes | After any STA save, sign in at the new device IP within 3 minutes or the previous network settings are restored. |
| **Boot with no credentials** | SoftAP up | See [AP](ap.md). |
| **Boot with credentials** | STA first | SoftAP only if STA does not associate in about **15 s**. Then AP+STA until STA connects; SoftAP is then stopped. |
| **STA drops after a successful join** | retry STA only | SoftAP is **not** raised automatically. Use USB `AP_START` or reboot. |
| **Timezone offset (min)** | UTC+0 | Wall-clock labels in shot history. |
| **NTP server** | pool | Preset or custom hostname for time sync. |

Factory credentials and the first-connection walkthrough are in the
[README](../../README.md#first-connection) and [AP](ap.md).

Scan lists up to **12** networks and times out after **120 s**. Cancel from
the same maintenance window.

## Example

Sign in at `http://192.168.4.1`, save your home SSID. The controller joins
STA, SoftAP stops, and the UI moves to the DHCP address. If you later lose
that network, the device keeps retrying STA. Recover the AP with USB
`AP_START` (see [USB serial CLI](../SERIAL_CLI.md)) or a reboot.

OTA over Wi-Fi is documented in [Build environment](../BUILD.md) once an
image is built. Change the AP password away from the factory value before
OTA will accept uploads.

Related: [AP](ap.md), [Factory reset](factory-reset.md).

# AP

Fallback Wi-Fi access point used when no home network is saved, or when saved
STA fails to associate at boot.

## When it applies

- Fresh flash or [factory reset](factory-reset.md): SoftAP is up and stays
  up (no idle shutdown timer).
- Saved STA fails to associate for about **15 s** at boot: SoftAP comes up
  and STA keeps retrying in AP+STA until STA connects. Then SoftAP stops.
- After a successful STA join, a later drop does **not** raise SoftAP.
  Use USB `AP_START` or reboot. See [USB serial CLI](../SERIAL_CLI.md).

The AP name is fixed. You can change the WPA2 password from **Admin → AP
password** (current + new) or from USB (`SET_AP_PASSWORD` /
`RESET_AP_PASSWORD`).

The same password is used to sign in to the Web UI for configuration. Status,
live shot, history, and the diagnostic log are visible without signing in.

## Parameters

| Setting | Default | Notes |
| --- | --- | --- |
| **AP name** | `MicraShotStopperAP` | Not user-editable. |
| **AP / Web UI password** | `Micra1234` | Case-sensitive. 8–63 characters when you change it; USB `SET_AP_PASSWORD` will not accept the factory string as the new value. |
| **AP address** | `http://192.168.4.1` | SoftAP IPv4. |

## First connection

1. Power the ESP32-S3 and wait for boot (the GPIO 1 LED stays off until a
   scale connects).
2. Join **`MicraShotStopperAP`** with password **`Micra1234`**.
3. Open **`http://192.168.4.1`**.
4. Sign in with **`Micra1234`** to save home Wi-Fi or change settings.

If you lose STA or the UI password, recover over this AP (after reboot /
`AP_START`), USB CLI, or [paddle emergency recovery](../EMERGENCY_RECOVERY.md).

Related: [Wi-Fi](wifi.md), [Factory reset](factory-reset.md).

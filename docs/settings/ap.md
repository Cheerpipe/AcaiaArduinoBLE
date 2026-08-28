# AP

Fallback Wi-Fi access point used when no home network is saved, or when saved
STA fails to associate at boot.

## When it applies

- Fresh flash or [factory reset](factory-reset.md): SoftAP is up at boot.
- Saved STA fails to associate for about **25 s** at boot: SoftAP comes up
  and STA keeps retrying in AP+STA until STA connects. Then SoftAP stops.
- SoftAP auto-raise is **boot/bootstrap only**. After a successful STA join,
  a later drop does **not** raise SoftAP. Use USB `AP_START` or reboot.
  See [USB serial CLI](../SERIAL_CLI.md).

## Idle shutdown

Auto SoftAP stays up for at most **3 minutes** with zero SoftAP stations.
If a phone or laptop joins the AP, SoftAP stays up while that client is
associated. When the client count returns to zero, a new 3-minute idle
countdown starts. After idle shutdown, SoftAP stays down for the rest of
the boot (USB `AP_START` or reboot to raise it again). Manual `AP_START`
keeps SoftAP up without the idle timer until `AP_STOP`.

The AP name is fixed. SoftAP WPA2 uses the **device password**. Change it
from **Admin** (unlock with the device password) **→ Device password** or from USB
(`SET_DEVICE_PASSWORD` / `RESET_DEVICE_PASSWORD`). Admin unlock and OTA use
that same device password.

## Parameters

| Setting | Default | Notes |
| --- | --- | --- |
| **AP name** | `AdvancedShotStopperAP` | Not user-editable. |
| **Device password** | `ineedacoffee` | Case-sensitive. 8–63 characters when you change it; USB `SET_DEVICE_PASSWORD` will not accept the factory string as the new value. SoftAP WPA2, Admin unlock, and OTA all use this same device password. |
| **AP address** | `http://192.168.4.1` | SoftAP IPv4. |

## First connection

1. Power the ESP32-S3 and wait for boot (the GPIO 1 LED stays off until a
   scale connects).
2. Join **`AdvancedShotStopperAP`** with the device password **`ineedacoffee`**.
3. Open **`http://192.168.4.1`** within the idle window (or stay associated).
4. Claim the Web UI to save home Wi-Fi or change settings.

If you lose STA or the device password, recover over this AP (after reboot /
`AP_START`), USB CLI, or [paddle emergency recovery](../EMERGENCY_RECOVERY.md).

Related: [Wi-Fi](wifi.md), [Factory reset](factory-reset.md).

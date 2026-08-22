# Factory reset

Erases saved configuration and returns the controller to first-boot
credentials. Firmware on the flash is not erased.

## What is erased

Wi-Fi (STA and last-known-good network), workflow settings, presets,
calibration (including learned offset and A→M samples), preferred scale,
BLE Companion preference, shot history, and last shot. The device
password returns to **`Micra1234`**. The device then restarts.

## Ways to run it

All of these need the machine idle where a safety gate exists (paddle OFF,
CN9 open, Ready) except the paddle gesture, which runs **before** Wi-Fi and
keeps CN9 open.

| Path | How | Notes |
| --- | --- | --- |
| **Web UI** | Admin → unlock with the device password → Factory reset, confirm `ERASE_ALL_SETTINGS` | Device password required. Device restarts. |
| **USB serial** | `FACTORY_RESET` | Same erase. See [USB serial CLI](../SERIAL_CLI.md). |
| **Paddle gesture** | Power on with paddle ON, then five `OFF→ON` cycles | Last-resort. See [Emergency recovery](../EMERGENCY_RECOVERY.md). |

A shorter paddle gesture (three cycles) restores the device password / forgets STA
**without** erasing recipes, calibration, or history.

## After reset

Connect to `MicraShotStopperAP` / `Micra1234` at `http://192.168.4.1` and set
the device up again. See [AP](ap.md) and the
[README first connection](../../README.md#first-connection).

Related: [Wi-Fi](wifi.md), [Emergency recovery](../EMERGENCY_RECOVERY.md),
[FAQ](../FAQ.md).

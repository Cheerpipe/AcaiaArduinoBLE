# OTA

Firmware can be updated over Wi-Fi without opening the case or using USB.
Upload a built image with the project scripts while the controller is on
your network.

## Requirements

- Change the device password away from the factory default
  (`Micra1234`). That password is the **OTA token**; scripts never store it.
- Pass the token every run (`--token` or `SHOTSTOPPER_OTA_TOKEN`).
- Image must match the board architecture and must not be older than the
  running version (downgrades are refused).

## Safety behavior

Updates use a dual slot. After reboot, the new image must successfully
serve the Web UI before it is confirmed. If it fails, the bootloader
rolls back to the previous slot—no USB required. A second OTA while
verification is still pending is refused (`PENDING_VERIFY`).

CN9 stays open during the update. Wi-Fi credentials, presets, calibration,
and shot history are left unchanged on a successful flash.

## How to run

Build and flash flow: [Build environment](../BUILD.md). Script flags and
`./scripts/ota` / IDF equivalents: [Build scripts](../SCRIPTS.md).

Related: [Wi-Fi](../settings/wifi.md), [AP](../settings/ap.md),
[Emergency recovery](../EMERGENCY_RECOVERY.md).

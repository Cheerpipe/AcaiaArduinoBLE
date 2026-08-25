# OTA

Firmware can be updated over Wi-Fi without opening the case or using USB.
Upload a built image with the project scripts while the controller is on
your network.

## Requirements

- Pass the **device password** every CLI run (`--password` / `-t`, or
  `SHOTSTOPPER_DEVICE_PASSWORD`). Scripts never store it. The factory
  default (`ineedacoffee`) works if it was never changed.
- From **Admin → Firmware update**, unlock administration first. The Web UI
  does not ask for the device password again.
- Image must match the board architecture and must not be older than the
  running version (downgrades are refused).

## Safety behavior

Updates use a dual slot. After reboot, the new image must successfully
serve the Web UI before it is confirmed. If it fails, the bootloader
rolls back to the previous slot—no USB required. A second OTA while
verification is still pending is refused (`PENDING_VERIFY`).

Machine circuit stays open during the update. Wi-Fi credentials, presets, calibration,
and shot history are left unchanged on a successful flash.

## How to run

Build and flash flow: [Build environment](../BUILD.md). Script flags and
`./scripts/ota` / IDF equivalents: [Build scripts](../SCRIPTS.md).

Related: [Wi-Fi](../settings/wifi.md), [AP](../settings/ap.md),
[Emergency recovery](../EMERGENCY_RECOVERY.md).

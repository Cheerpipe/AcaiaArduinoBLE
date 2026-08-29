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

The shot always has priority. The paddle is never blocked by an update.

- **No upload or flash during a shot.** Starting an update while the machine is pouring is refused (`CONFIG_LOCKED_DURING_ACTIVE_CYCLE`) so Wi-Fi does not compete with the scale.
- **Paddle during transfer aborts the upload** (`SAFETY_LOST`). The spare slot is discarded; the running firmware is untouched. A verified staged image is kept if you pull a shot *after* verify — flash waits until idle.
- **No planned restart during a shot.** Flash, Admin Restart, and serial `REBOOT` wait until the pour ends and the circuit is open. They do not open the relay to make way for the reset.
- Confirming or rolling back a `PENDING_VERIFY` image writes otadata (flash cache off). That write is deferred while a shot is pouring or GATT is up.

Updates use a dual slot. After reboot, the new image boots as
`PENDING_VERIFY` and is confirmed only after the Web UI has been
serving for at least 15 s (HTTP up is the proof — not brew or BLE).
A second OTA while verification is still pending is refused
(`PENDING_VERIFY`).

If HTTP never comes up, the controller waits up to 180 s, then:

- **Previous slot bootable:** the running image is marked invalid and
  the bootloader rolls back on restart — no USB required. That restart
  still waits if a shot is in progress.
- **No bootable previous slot:** the running image is confirmed so the
  machine is not left without an application. Recover over USB; see
  [Emergency recovery](../EMERGENCY_RECOVERY.md).

Watchdog and panic still open the circuit and reset immediately: a hung
firmware cannot wait for the shot to finish.

Wi-Fi credentials, presets, calibration, and shot history are left
unchanged on a successful flash.

## How to run

OTA can be performed from the Web UI or from the command line. In both cases,
the controller and the computer must be on the same Wi-Fi network (or the
controller's SoftAP network).

### Web UI

Open **Admin → Firmware update**, unlock Admin with the device password, then
select the firmware `.bin`. Choose **Upload and verify** and, once verification
succeeds, select **Flash and restart**. The controller IP is shown on the
Admin page; when using its SoftAP it is always `192.168.4.1`.

### Command line (CLI)

Use the project scripts from the repository root. The CLI authenticates with
the device password; pass it with `--password` (or `-t`) or enter it when the
script prompts. The password is never saved by the scripts.

Build and upload in one command:

```sh
./scripts/bo-idf --arch n16r8 --host 192.168.1.50 --password "my-device-password"
```

Upload an image that was already built:

```sh
./scripts/ota-idf --arch n16r8 --host 192.168.1.50 --password "my-device-password"
```

Upload a firmware image stored elsewhere (for example, a release downloaded
outside this repository):

```sh
./scripts/ota-idf --arch n16r8 --host 192.168.1.50 --password "my-device-password" \
  --image ~/Downloads/shotstopper.bin
```

For a controller using its SoftAP, replace the host with `192.168.4.1`:

```sh
./scripts/bo-idf -a n16r8 -H 192.168.4.1 -t "my-device-password"
```

Replace `n16r8` with `n8r4` if that is your board. `bo-idf` builds first;
`ota-idf` uses `build-idf/<architecture>/shotstopper.bin` and therefore
requires that you have run `build-idf` already. Short aliases `o-idf` and
`bo-idf` are also available.

Build and flash flow: [Build environment](../BUILD.md). Script flags and
CLI reference: [Build scripts](../SCRIPTS.md).

Related: [Wi-Fi](../settings/wifi.md), [AP](../settings/ap.md),
[Emergency recovery](../EMERGENCY_RECOVERY.md).

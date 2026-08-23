# USB serial CLI

Shot Stopper accepts **line-based commands** on the same USB serial port as
the logs (**115200** baud). Verbs are case-insensitive. SSIDs and passwords
are case-sensitive; wrap values with spaces in double quotes.

These commands are the same whether you built with ESP-IDF or the legacy
Arduino-cli path. How you **open the port** differs. Build/flash wrappers
are in [Build scripts](SCRIPTS.md), not here.

## Open the port

**Supported (ESP-IDF):**

```sh
./scripts/monitor-idf --port /dev/cu.usbmodem2101 --speed 115200
```

On Linux the port is often `/dev/ttyACM0` or `/dev/ttyUSB0`. Exit with
**Ctrl+]**. The script prompts for and remembers the port in `.shotstopper`.

**Legacy Arduino-cli (unsupported):**

```sh
./scripts/monitor --port /dev/cu.usbmodem2101 --speed 115200
```

Prefer `monitor-idf` even if you only need this CLI.

Do not type into a scrolling monitor if you cannot see what you send. Close
any other serial client, then pipe the command:

```sh
(sleep 4; printf 'HELP\n'; sleep 2) | ./scripts/monitor-idf -p /dev/cu.usbmodem2101 -s 115200
```

The `sleep 4` waits for the USB-serial chip to reopen (opening the port often
resets the ESP32).

Successful mutating commands print `OK queued …`, `OK …`, or a status dump.
Rejections print `ERR …`. Passwords are never echoed.

After `REBOOT` / `SET_WIFI` / `CLEAR_WIFI` / `FACTORY_RESET` the board
restarts and the monitor session may drop.

## Safety gate

Destructive commands need the same gate as the Web UI: physical paddle
**OFF**, machine circuit open, state **Ready**, no active cycle.

If USB/serial is also unavailable, use the
[paddle emergency recovery procedure](EMERGENCY_RECOVERY.md). It works before
Wi-Fi and BLE startup and keeps machine circuit open throughout the operation.

Always allowed (including during a cycle):

- `HELP`, `HELLO`
- dumps (`*_STATUS`, `NET_STATUS`, `LOG_DUMP`, `HEALTH`)
- `SERIAL_DEBUG_ON` / `SERIAL_DEBUG_OFF`, `DEBUG_FULL` / `DEBUG_OFF`
- link mutations (`WIFI_CONNECT` / `DISCONNECT` / `RESTART`, `AP_START` /
  `AP_STOP`, `WEBUI_START` / `STOP` / `RESTART`)

Link mutations print `WARN cycle active; proceeding` if a shot is running.

## Holds

`WIFI_DISCONNECT`, `AP_STOP`, and `WEBUI_STOP` stay in effect until the
matching `CONNECT` / `START` / `RESTART` or a reboot. Automatic STA retry,
boot SoftAP fallback (only before a successful STA join this session), and
HTTP retry do not undo those stops.

`AP_START` keeps SoftAP up even if STA is already connected (AP+STA). After a
successful STA join, SoftAP is not auto-raised on link loss — use `AP_START` or
reboot. Automatic SoftAP from boot is still torn down when STA comes up.
`WEBUI_STOP` is not undone by `AP_START`.

## Probe and help

| Command | Parameters | Effect |
| --- | --- | --- |
| `HELP` | none | Prints one-line summaries and examples |
| `HELLO` | none | Replies `how are you` |

## Device

| Command | Parameters | Effect |
| --- | --- | --- |
| `REBOOT` | none | Restarts firmware (safety gate) |
| `FACTORY_RESET` | none | Wipes Wi-Fi, settings, calibration, and shots; device password `ineedacoffee`; restarts (safety gate) |
| `SET_DEVICE_PASSWORD` | `<password>` | Sets the device password (8–63 chars, not `ineedacoffee`). Used for SoftAP WPA2 and OTA. Does not require the current password. Safety gate |
| `RESET_DEVICE_PASSWORD` | none | Restores device password `ineedacoffee`. STA unchanged. Safety gate |

## STA credentials (persist + reboot)

`SET_WIFI` always uses **DHCP**. Set a static IP from the Web UI.

| Command | Parameters | Effect |
| --- | --- | --- |
| `SET_WIFI` | `<ssid> [password]` | Saves home Wi-Fi (DHCP), **commits** it (no 3-minute Web UI confirm window), and restarts. Omit password if open. Quote spaces. Safety gate |
| `CLEAR_WIFI` | none | Forgets saved STA only; restarts (safety gate) |
| `RESET_NETWORK_AP` | none | Forgets STA and restores device password `ineedacoffee`; restarts (safety gate) |

## STA link (no NVS change)

| Command | Parameters | Effect |
| --- | --- | --- |
| `WIFI_CONNECT` | none | Associates the saved STA. Errors if none is configured |
| `WIFI_DISCONNECT` | none | Drops STA and holds reconnect. SoftAP auto-raise only if STA never connected this session; otherwise use `AP_START` |
| `WIFI_RESTART` | none | Drops then reconnects saved STA (no ESP reboot) |
| `WIFI_STATUS` | none | Dumps STA config, link, timers, and holds |

## SoftAP

| Command | Parameters | Effect |
| --- | --- | --- |
| `AP_START` | none | Raises SoftAP (`AdvancedShotStopperAP` at `192.168.4.1`). Stays up if STA is connected. Does not start HTTP if `WEBUI_STOP` is held |
| `AP_STOP` | none | Stops SoftAP and holds auto-raise. HTTP stays if STA is up |
| `AP_STATUS` | none | Dumps SoftAP state (never the password) |

## Web UI

| Command | Parameters | Effect |
| --- | --- | --- |
| `WEBUI_START` | none | Starts the HTTP server |
| `WEBUI_STOP` | none | Stops HTTP and holds auto-start |
| `WEBUI_RESTART` | none | Bounces HTTP. Sessions in RAM survive |
| `WEBUI_STATUS` | none | Dumps HTTP / session / bind state |

## Network shortcut

| Command | Parameters | Effect |
| --- | --- | --- |
| `NET_STATUS` | none | Prints `WIFI_STATUS`, then `AP_STATUS`, then `WEBUI_STATUS` |

## Debug

`SERIAL_DEBUG_ON` turns USB traces on at **info**. `DEBUG_FULL` turns USB
traces on at **debug** and sets the Web UI log ring to **debug**. Both
persist.

| Command | Parameters | Effect |
| --- | --- | --- |
| `SERIAL_DEBUG_ON` | none | USB traces at info; ring unchanged |
| `SERIAL_DEBUG_OFF` | none | USB traces off; ring unchanged. Replies before silencing |
| `DEBUG_FULL` | none | USB traces + ring at debug |
| `DEBUG_OFF` | none | USB traces off and ring none. Replies before silencing |
| `DEBUG_STATUS` | none | Shows `serialDebugOutput`, `serialLogLevel`, `ringRetainLogLevel` |

## Diagnostics

| Command | Parameters | Effect |
| --- | --- | --- |
| `LOG_DUMP` | none | Prints the RAM debug ring (oldest first), one event at a time. Deferred while a cycle is active or machine circuit is closed. Says so if empty or retain is none |
| `HEALTH` | none | Heap, PSRAM, BLE host alloc counters, loop gap (interval + max), task stacks, CPU load, temperature, alert latches |
| `SCALE_STATUS` | none | BLE scale link, preferred MAC/name, weight freshness |
| `NTP_STATUS` | none | Wall clock / NTP state. Notes if STA is down |
| `BLE_COMPAT_ENABLE` | none | Enables the ShotStopper Companion GATT profile on the next boot; restart required |
| `BLE_COMPAT_DISABLE` | none | Disables the Companion GATT profile on the next boot so its RAM is not allocated; restart required |
| `BLE_COMPAT_STATUS` | none | Configured next-boot state, active-this-boot state, restart requirement, protocol, advertising, client/AP state, and write counters |

## Shot history

| Command | Parameters | Effect |
| --- | --- | --- |
| `CLEAR_SHOTS` | none | Clears recorded shot history (safety gate) |

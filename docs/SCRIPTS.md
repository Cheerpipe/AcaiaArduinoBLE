# Build scripts

Developer scripts under `scripts/`. Walkthrough from clone to flash:
[Build environment](BUILD.md).

**Supported: the `*-idf` scripts.** The Arduino-cli scripts are legacy and
**unsupported**.

These are **not** the USB firmware commands (`HELP`, `FACTORY_RESET`, …).
Those live in [USB serial CLI](SERIAL_CLI.md).

## How parameters are resolved

No script silently fills missing values. Each parameter comes, in order,
from:

1. A named flag
2. Its environment variable
3. The `.shotstopper` file at the repository root
4. An interactive prompt (Enter accepts the value in brackets)

If `--port` is missing, or the saved/CLI path is not a present device node,
flash and monitor scripts prompt like OTA does for the device password: they
list detected USB-CDC ports (`/dev/cu.usbmodem*` on macOS, `/dev/ttyACM*` on
Linux), suggest the first match, and accept Enter or a typed path. The chosen
port is saved to `.shotstopper`.

App CDC enumerates only when **GPIO 4 is jumpered to GND at reset**
([Hardware](HARDWARE.md)), unless you compiled with
`-DSHOT_STOPPER_ENABLE_JTAG=1`. Without the jumper, `monitor-idf` has no port
while the app is running. `flash-idf` still works via **BOOT + RST** (ROM
USB download) or use **OTA**. ROM download does not need the jumper.

After a successful run, non-secret values are saved, so the next command can
be just `./scripts/bfm-idf`. The **device password is never stored or
suggested** — pass `--password` / `-t` or `SHOTSTOPPER_DEVICE_PASSWORD`
every time.

`.shotstopper` is created mode `600` and is gitignored.

For CI or a non-TTY terminal, pass flags or environment variables. If
anything required is missing, the script exits with an error instead of
prompting. The same applies with `SHOTSTOPPER_NONINTERACTIVE=1`.

## Flags

| Flag | Environment variable | Meaning |
| --- | --- | --- |
| `-p`, `--port` | `SHOTSTOPPER_PORT` | Serial port, e.g. `/dev/cu.usbmodem2101` (macOS) or `/dev/ttyACM0` (Linux). |
| `-a`, `--arch` | `SHOTSTOPPER_ARCH` | `n8r4` or `n16r8` (alias `esp32s3` → `n16r8`). |
| `-s`, `--speed` | `SHOTSTOPPER_SPEED` | Serial monitor baud, e.g. `115200`. |
| `-H`, `--host` | `SHOTSTOPPER_HOST` | Controller IP or hostname for OTA. |
| `-t`, `--password` | `SHOTSTOPPER_DEVICE_PASSWORD` | Device password. Never persisted. |
| `-f`, `--flags` | `SHOTSTOPPER_FLAGS` | Extra compile flags, as a single string. |
| `-i`, `--image` | `SHOTSTOPPER_IMAGE` | Firmware `.bin` to use for flash or OTA instead of the normal build output. Checked locally and never persisted. |
| `-b`, `--build-dir` | `SHOTSTOPPER_BUILD_DIR_OVERRIDE` | Build directory (`static` legacy only). |
| `-o`, `--output-dir` | `SHOTSTOPPER_OUTPUT_DIR` | Reports directory (`static` / `static-idf` only). |
| `-h`, `--help` | — | Show the script help. |

Suggested `--flags` at the prompt (Enter accepts them):
`-Werror=deprecated-copy -DSHOT_STOPPER_ENABLE_BUZZER=1`.
Remote machine control stays off unless you add
`-DSHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=1`.

For local development only, add `-DSHOT_STOPPER_DEVELOPMENT=1` to bypass WebUI
admin unlock (Admin / Diagnostic / Home Actions without the device password).
Do not ship development builds to production devices.

USB Serial/JTAG stays **off** unless you add `-DSHOT_STOPPER_ENABLE_JTAG=1`.
That build turns the IDF USB Serial/JTAG console on at boot (OpenOCD + CDC,
no GPIO4 jumper). Default firmware and any flash without that flag keep JTAG
off.

`idf.py` does not reconfigure when only the environment changes. Changing
`--flags` (for example `-DSHOT_STOPPER_MACHINE_TYPE=1`) drops the IDF CMake
cache so the new `-D` flags actually reach the compiler. Diagnostic **Type**
is that compile-time machine type (`paddle`, `momentary`, or `momentary_reed`),
not a runtime setting.

## ESP-IDF (supported)

Writes to `build-idf/<architecture>` (`shotstopper.bin`).

| Script | Alias | Required | Description |
| --- | --- | --- | --- |
| `./scripts/build-idf` | `b-idf` | `--arch` (`--flags` optional) | Generate version and Web UI, build with ESP-IDF. |
| `./scripts/flash-idf` | `f-idf` | `--port`, `--arch` | Flash the existing binary (or `--image <path>`); does not rebuild or open the monitor. |
| `./scripts/monitor-idf` | `m-idf` | `--port`, `--speed` | IDF serial monitor (Ctrl+] to exit). |
| `./scripts/ota-idf` | `o-idf` | `--arch`, `--host`, `--password` | Wi-Fi update with the already-built IDF binary, or `--image <path>`. |
| `./scripts/static-idf` | `s-idf` | `--arch` | Cppcheck against the IDF compilation database. Does not build. |
| `./scripts/bf-idf` | | `--port`, `--arch` | build-idf then flash-idf. |
| `./scripts/bfm-idf` | | `--port`, `--arch`, `--speed` | build-idf, flash-idf, monitor-idf. |
| `./scripts/bo-idf` | | `--arch`, `--host`, `--password` | build-idf then ota-idf. |
| `./scripts/bsfm-idf` | | `--port`, `--arch`, `--speed` | build-idf, static-idf, flash-idf, monitor-idf. Does not flash if analysis reports diagnostics. |
| `./scripts/gcc_analyzer` | | `--arch` (`--flags` optional) | Build with GCC `-fanalyzer` into `reports/gcc-analyzer/`. |

Examples:

```sh
# First run: prompts for what is missing and remembers it
./scripts/bfm-idf

# Explicit (macOS CDC port)
./scripts/build-idf --arch n16r8
./scripts/flash-idf --port /dev/cu.usbmodem2101 --arch n16r8
./scripts/flash-idf --port /dev/cu.usbmodem2101 --arch n16r8 --image ~/Downloads/shotstopper.bin
./scripts/monitor-idf -p /dev/cu.usbmodem2101 -s 115200

# Linux
./scripts/bfm-idf -p /dev/ttyACM0 -a n16r8 -s 115200

# Wi-Fi update
./scripts/bo-idf --arch n16r8 --host 192.168.1.50
./scripts/o-idf --arch n16r8 --host 192.168.1.50
./scripts/o-idf --arch n16r8 --host 192.168.1.50 --image ~/Downloads/shotstopper.bin
```

`--image` can also be passed to the flash/OTA wrappers (`bf`, `bfm`, `bo`,
`bsfm` and their `*-idf` variants); they still execute their named build or
analysis steps, then use the selected image for the transfer. The image is
checked against the selected `--arch` before transfer. For an IDF USB flash,
an external image is written to the app partition at `0x10000`; it does not
replace the bootloader or partition table.

## Arduino-cli (unsupported)

Writes to `build/<architecture>`. Do **not** use for production firmware.
See [Build environment](BUILD.md) for why.

| Script | Alias | Description |
| --- | --- | --- |
| `./scripts/build` | `b` | Build with arduino-cli. |
| `./scripts/flash` | `f` | Flash the Arduino-cli image, or `--image <path>`. |
| `./scripts/monitor` | `m` | Arduino-cli serial monitor. |
| `./scripts/ota` | `o` | OTA with the Arduino-cli image, or `--image <path>`. |
| `./scripts/static` | `s` | Cppcheck against the Arduino-cli build directory. |
| `./scripts/bf` / `bfm` / `bo` / `bsfm` | | Wrappers, same idea as the `*-idf` variants. |

Required flags match the IDF table (`--arch`, `--port`, and so on). Prefer
`./scripts/monitor-idf` over `./scripts/monitor` even when you only want the
[USB serial CLI](SERIAL_CLI.md).

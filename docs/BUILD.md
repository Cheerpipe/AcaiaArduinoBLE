# Build environment

Step-by-step from a git clone to a flashable firmware image.

**Official supported path: ESP-IDF only.** Arduino / arduino-cli scripts still
exist in the tree for transition; they are **not supported**. See
[Build scripts](SCRIPTS.md).

This page documents **macOS** and **Linux**. The maintainer develops on
**macOS**. Windows is not documented yet.

You need:

- Git
- Node.js (for the embedded Web UI generator)
- A C++ compiler (for host tests)
- ESP-IDF **5.5.x** (this project tracks **5.5.5**)
- An ESP32-S3 with PSRAM: **n16r8** (16 MB flash, 8 MB OPI PSRAM) or
  **n8r4** (8 MB flash, 4 MB QSPI PSRAM)

The development board in [Hardware](HARDWARE.md) is an N16R8 module. Use
`--arch n16r8` unless you know you have N8R4.

Do not install EspressoScaleBLE from Library Manager. The audited copy is in
`libraries/EspressoScaleBLE`.

## 1. Clone the repository

```sh
git clone <this-repository-url>
cd AcaiaArduinoBLE
```

Use your fork’s URL if that is where you work.

## 2. Node.js dependencies

Once, from the repository root:

```sh
npm install
```

This installs Terser and related tools used to minify and gzip the Web UI.

## 3. Install ESP-IDF (required)

Install ESP-IDF **5.5.x**. The build script **refuses any version outside
5.5.x**. Arduino-ESP32 is pinned to **3.3.11** in
`idf/main/idf_component.yml`. Keep `idf/dependencies.lock` committed so every
machine resolves the same graph.

### macOS (maintainer)

```sh
mkdir -p "$HOME/esp" && cd "$HOME/esp"
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
. ./export.sh
idf.py --version
```

Homebrew `node` is enough for the Web UI. A system C++ compiler
(Xcode command-line tools) is enough for host tests:

```sh
xcode-select --install   # if clang++ is missing
```

Optional static analysis:

```sh
brew install cppcheck
```

Optional coverage reports need LLVM (`llvm-profdata`, `llvm-cov`).

### Linux

Same IDF clone as above. Install Node.js from your distro or
[nodejs.org](https://nodejs.org/), and a C++ toolchain (`g++` or `clang++`).

On Debian/Ubuntu, a typical extra set is:

```sh
sudo apt-get update
sudo apt-get install git python3 python3-pip python3-venv cmake ninja-build \
  wget flex bison gperf ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 \
  nodejs npm g++
```

Then install ESP-IDF as in the macOS block (`install.sh esp32s3` and
`export.sh`).

If `idf.py --version` is not 5.5.x, the Shot Stopper scripts will stop.

`./scripts/build-idf` sources `export.sh` from `IDF_PATH` or
`$HOME/esp/esp-idf` when needed.

## 4. ArduinoBLE patch (handled for you)

The first IDF build clones ArduinoBLE **2.1.0** into the IDF tree and runs
`./scripts/patch_arduinoble.sh`. You do not need to run that by hand.
`./scripts/build-idf` does it. The patch is idempotent. Set `ARDUINO_BLE_HOME`
only if ArduinoBLE is not in the default path.

## 5. Build

From the repository root:

```sh
./scripts/build-idf --arch n16r8
```

For N8R4: `--arch n8r4`. There is no silent default; `--arch` is asked once
and then remembered in `.shotstopper`.

`build-idf` already runs `./scripts/gen_version.sh` and
`node ./scripts/gen_web_ui.js`. Output is
`build-idf/<arch>/shotstopper.bin`.

Extra compile defines go in `--flags`. The prompt’s suggested flags enable a
passive buzzer only. Remote machine control is **opt-in** and off by default
(`SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=0` or the flag omitted). Enter
accepts the suggested flags. An existing `build-idf/<arch>/sdkconfig` that
already has remote control on keeps that choice until you delete the tree or
change it in menuconfig.

Examples:

```sh
./scripts/build-idf --arch n16r8 \
  --flags "-Werror=deprecated-copy -DSHOT_STOPPER_ENABLE_BUZZER=1"
```

Opt-in remote start/rinse (trusted network only):

```sh
./scripts/build-idf --arch n16r8 \
  --flags "-Werror=deprecated-copy -DSHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=1 -DSHOT_STOPPER_ENABLE_BUZZER=1"
```

- `SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=1` — virtual paddle and remote rinse on the
  Web UI (trusted network only). Omit or `=0` — only the physical activator can
  close the machine circuit. **Stop** is always available when signed in.
- `SHOT_STOPPER_ENABLE_BUZZER=1` — passive piezo (PWM/RTTTL). `=0` or omit — no local buzzer.

N16R8 uses a 16 MB OTA table (about **3 MB** app slot × 2). N8R4 uses the 8 MB
table (about **3.2 MB** × 2). A 4 MB default table cannot hold this firmware.

## 6. Flash (USB)

Connect the board. On macOS the native USB CDC port is usually
`/dev/cu.usbmodemXXXX` (use `cu.*`, not `tty.*`). On Linux it is often
`/dev/ttyACM0` or `/dev/ttyUSB0`.

```sh
./scripts/flash-idf --port /dev/cu.usbmodem2101 --arch n16r8
```

Or build and flash in one step:

```sh
./scripts/bf-idf
./scripts/bfm-idf    # build, flash, then serial monitor
```

The first run prompts for the port and remembers it. Baud for this firmware
is **115200**.

## 7. Serial monitor and CLI

```sh
./scripts/monitor-idf --port /dev/cu.usbmodem2101 --speed 115200
```

Exit the IDF monitor with **Ctrl+]**. Firmware commands are listed in
[USB serial CLI](SERIAL_CLI.md). USB debug print is **off** by default;
enable it from the Web UI Debug page or `SERIAL_DEBUG_ON`.

If the monitor shows garbage, confirm the port, close other serial programs,
and stay on **115200**. Press RST on the board after opening the monitor.

## 8. Host tests (before you flash)

```sh
npm install
./libraries/EspressoScaleBLE/tests/run_host_tests.sh
./shotStopper/tests/run_host_tests.sh
node ./shotStopper/tests/check_web_assets.js
node ./shotStopper/tests/check_firmware_size.js build-idf/n16r8/shotstopper.bin
```

Skip the size check if you have not compiled yet. Coverage (optional):

```sh
./shotStopper/tests/run_coverage.sh
```

Automated tests do not replace the [manual test plan](MANUAL_TEST_PLAN.md).

## 9. Update over Wi-Fi (OTA)

After the controller is installed, USB is inconvenient. Both partition tables
have two application slots. Build, then:

```sh
./scripts/bo-idf --arch n16r8 --host 192.168.1.50
```

`bo-idf` compiles with ESP-IDF and runs `./scripts/ota-idf`. The same flow
is available from **Admin → Firmware update** (*Upload and verify*, then
*Flash and restart*). The controller IP is on the Admin page. In SoftAP mode
it is always `192.168.4.1`.

OTA uses the **device password** (`--password` / `SHOTSTOPPER_DEVICE_PASSWORD`)
for the command-line client. The Web UI authorises the same update after you
unlock **Admin** with that same device password.

The running firmware is never overwritten; the upload goes to the inactive
slot. Settings in saved storage survive. If an update fails, the device keeps
the firmware it already had. USB `./scripts/flash-idf` is always the way out.

Full command tables: [Build scripts](SCRIPTS.md).

## Arduino / arduino-cli (unsupported)

`./scripts/build` and related helpers remain for transition only. Prebuilt
Arduino-ESP32 libraries do not move large buffers to PSRAM the way the IDF
project does. **Do not use them for production firmware.**

If you still inspect that path:

```sh
arduino-cli version
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install ArduinoBLE@2.1.0
./scripts/patch_arduinoble.sh
```

Commands: [Build scripts](SCRIPTS.md).

## Optional static analysis

```sh
./scripts/build-idf --arch n16r8
./scripts/static-idf --arch n16r8
```

Reports go to `reports/static-analysis/`. Deeper GCC analysis:

```sh
./scripts/gcc_analyzer
```

## Firmware version

Release version lives in `VERSION` at the repository root. The git hash is
appended on every build (`1.0.0+abc1234`, or `-dirty` with uncommitted
changes). The Web UI footer and serial boot line show that string.

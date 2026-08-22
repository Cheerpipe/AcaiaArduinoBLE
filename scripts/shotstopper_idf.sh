#!/usr/bin/env bash
# Shared ESP-IDF helpers. Source after shotstopper_board.sh and
# shotstopper_cli.sh; do not execute this file directly.
#
# Official supported firmware builds write to build-idf/<architecture>.
# Legacy arduino-cli artifacts stay under build/ and are unsupported.

IDF_PROJECT_NAME="shotstopper"
IDF_DEFAULT_HOME="${HOME}/esp/esp-idf"
ARDUINO_BLE_VERSION="2.1.0"
ARDUINO_BLE_REPO="https://github.com/arduino-libraries/ArduinoBLE.git"

ss_idf_find() {
  if [[ -n "${IDF_PATH:-}" && -f "${IDF_PATH}/export.sh" ]]; then
    printf '%s' "$IDF_PATH"
    return 0
  fi
  if [[ -f "${IDF_DEFAULT_HOME}/export.sh" ]]; then
    printf '%s' "$IDF_DEFAULT_HOME"
    return 0
  fi
  return 1
}

ss_idf_source() {
  local idf_root
  idf_root="$(ss_idf_find)" || {
    echo "ESP-IDF not found (idf.py / export.sh)." >&2
    echo "Install 5.5.x and run again:" >&2
    echo "  mkdir -p \"\$HOME/esp\" && cd \"\$HOME/esp\"" >&2
    echo "  git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git" >&2
    echo "  cd esp-idf && ./install.sh esp32s3 && . ./export.sh" >&2
    exit 127
  }
  unset IDF_PYTHON_ENV_PATH ESP_PYTHON
  # IDF 5.5 supports Python 3.9–3.13. Homebrew python3 can be 3.14+.
  if [[ -x /opt/homebrew/opt/python@3.12/libexec/bin/python3 ]]; then
    PATH="/opt/homebrew/opt/python@3.12/libexec/bin:${PATH}"
    export PATH
  elif command -v python3.12 >/dev/null 2>&1; then
    PATH="$(dirname "$(command -v python3.12)"):${PATH}"
    export PATH
  fi
  # shellcheck disable=SC1091
  . "${idf_root}/export.sh"
  command -v idf.py >/dev/null 2>&1 || {
    echo "export.sh from ${idf_root} did not put idf.py on PATH." >&2
    exit 127
  }
  ss_idf_require_version
  if [[ "${SS_IDF_QUIET:-}" != "1" ]]; then
    echo "ESP-IDF: ${idf_root}"
  fi
}

# Shot Stopper is validated against ESP-IDF 5.5.x (tracks 5.5.5 with
# Arduino-ESP32 3.3.11). Refuse other majors/minors to avoid silent drift.
ss_idf_require_version() {
  local ver_line ver
  ver_line="$(idf.py --version 2>/dev/null | head -n1 || true)"
  ver="$(printf '%s' "$ver_line" | sed -n 's/.*v\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')"
  if [[ -z "$ver" ]]; then
    ver="$(printf '%s' "$ver_line" | sed -n 's/.*v\([0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')"
  fi
  case "$ver" in
    5.5|5.5.*)
      if [[ "${SS_IDF_QUIET:-}" != "1" ]]; then
        echo "ESP-IDF version: v${ver} (required: 5.5.x)"
      fi
      ;;
    *)
      echo "ESP-IDF 5.5.x is required (project validated with v5.5.5)." >&2
      echo "Found: ${ver_line:-unknown} (parsed: ${ver:-none})" >&2
      echo "Install or point IDF_PATH at v5.5.5:" >&2
      echo "  git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git" >&2
      exit 127
      ;;
  esac
}

# Call after shotstopper_resolve_board. Sets IDF_PROJECT, IDF_BUILD_DIR,
# IDF_IMAGE, IDF_ELF, IDF_MAP, IDF_SDKCONFIG, IDF_SDKCONFIG_DEFAULTS.
ss_idf_resolve_paths() {
  IDF_PROJECT="$SS_CLI_ROOT/idf"
  IDF_BUILD_DIR="$SS_CLI_ROOT/build-idf/$SHOTSTOPPER_ARCH"
  IDF_IMAGE="$IDF_BUILD_DIR/${IDF_PROJECT_NAME}.bin"
  IDF_ELF="$IDF_BUILD_DIR/${IDF_PROJECT_NAME}.elf"
  IDF_MAP="$IDF_BUILD_DIR/${IDF_PROJECT_NAME}.map"
  IDF_SDKCONFIG="$IDF_BUILD_DIR/sdkconfig"
  IDF_SDKCONFIG_DEFAULTS="$IDF_PROJECT/sdkconfig.defaults;$IDF_PROJECT/sdkconfig.defaults.$SHOTSTOPPER_ARCH"
}

ss_idf_py_args() {
  ss_idf_resolve_paths
  IDF_PY_ARGS=(
    -C "$IDF_PROJECT"
    -B "$IDF_BUILD_DIR"
    -D "SDKCONFIG=$IDF_SDKCONFIG"
    -D "SDKCONFIG_DEFAULTS=$IDF_SDKCONFIG_DEFAULTS"
  )
}

ss_idf_ensure_arduino_ble() {
  local dest="${1:-$IDF_PROJECT/third_party/ArduinoBLE}"
  local props="$dest/library.properties"
  local need_clone=0

  if [[ ! -f "$dest/src/ArduinoBLE.h" ]]; then
    need_clone=1
  elif [[ -f "$props" ]] && ! grep -q "^version=${ARDUINO_BLE_VERSION}$" "$props"; then
    echo "ArduinoBLE at $dest is not ${ARDUINO_BLE_VERSION}; recloning." >&2
    rm -rf "$dest"
    need_clone=1
  fi

  if [[ "$need_clone" -eq 1 ]]; then
    echo "Cloning ArduinoBLE ${ARDUINO_BLE_VERSION} → $dest"
    mkdir -p "$(dirname "$dest")"
    git clone --depth 1 --branch "$ARDUINO_BLE_VERSION" "$ARDUINO_BLE_REPO" "$dest"
  fi

  if [[ ! -f "$props" ]] || ! grep -q "^version=${ARDUINO_BLE_VERSION}$" "$props"; then
    echo "ArduinoBLE at $dest does not declare version=${ARDUINO_BLE_VERSION}." >&2
    exit 1
  fi

  # Stock GAP scan 20/20, OOM-safe discover, and BLE host PSRAM (same as Arduino-CLI).
  ARDUINO_BLE_HOME="$dest" "$SS_CLI_ROOT/scripts/patch_arduinoble.sh"
}

ss_idf_probe_in_psram() {
  local addr="$1"
  # ESP32-S3 maps instruction/data external RAM around 0x3C000000.
  # Internal DRAM BSS is typically 0x3FC00000.
  case "$addr" in
    0x3c*|0x3C*) return 0 ;;
  esac
  return 1
}

ss_idf_require_image() {
  ss_idf_resolve_paths
  if [[ ! -f "$IDF_IMAGE" ]]; then
    echo "$IDF_IMAGE does not exist." >&2
    echo "Build first: ./scripts/build-idf --arch $SHOTSTOPPER_ARCH" >&2
    exit 1
  fi
}

# ALLOW_BSS / autostart / g_probe, then the same OTA identity check as
# ./scripts/build-idf (image_tag.js).
ss_idf_verify_firmware() {
  ss_idf_resolve_paths

  if [[ ! -f "$IDF_SDKCONFIG" ]]; then
    echo "$IDF_SDKCONFIG does not exist; the IDF build left no sdkconfig." >&2
    exit 1
  fi
  if ! grep -q '^CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y$' "$IDF_SDKCONFIG"; then
    echo "ALLOW_BSS is not enabled in $IDF_SDKCONFIG." >&2
    grep 'SPIRAM_ALLOW_BSS' "$IDF_SDKCONFIG" >&2 || true
    exit 1
  fi
  echo "sdkconfig: CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y"

  if ! grep -q '^CONFIG_AUTOSTART_ARDUINO=y$' "$IDF_SDKCONFIG"; then
    echo "CONFIG_AUTOSTART_ARDUINO is not enabled in $IDF_SDKCONFIG." >&2
    exit 1
  fi
  echo "sdkconfig: CONFIG_AUTOSTART_ARDUINO=y"

  if [[ ! -f "$IDF_ELF" ]]; then
    echo "$IDF_ELF does not exist." >&2
    exit 1
  fi
  command -v xtensa-esp32s3-elf-nm >/dev/null 2>&1 || {
    echo "xtensa-esp32s3-elf-nm not found (did you run export.sh?)." >&2
    exit 127
  }

  local nm_line addr
  nm_line="$(xtensa-esp32s3-elf-nm "$IDF_ELF" | grep -E '[[:space:]]g_probe$|[[:space:]]_ZL[0-9]+g_probe$' || true)"
  if [[ -z "$nm_line" ]]; then
    echo "nm did not find g_probe in $IDF_ELF." >&2
    exit 1
  fi
  addr="0x$(printf '%s' "$nm_line" | awk '{print $1}')"
  echo "g_probe: $nm_line"

  if ss_idf_probe_in_psram "$addr"; then
    echo "BSS probe in PSRAM ($addr). ALLOW_BSS works with this toolchain."
  else
    echo "BSS probe is NOT in PSRAM ($addr). Expected 0x3c.... Internal DRAM is 0x3fc....." >&2
    if [[ -f "$IDF_MAP" ]]; then
      echo ".map excerpt:" >&2
      grep -n -A2 -E 'g_probe|ext_ram\.bss|extern_ram_seg' "$IDF_MAP" | head -40 >&2 || true
    fi
    exit 1
  fi

  local symbol
  for symbol in shotLog shotCurves persistedSettings; do
    nm_line="$(xtensa-esp32s3-elf-nm "$IDF_ELF" | grep -E "[[:space:]]${symbol}$" || true)"
    if [[ -z "$nm_line" ]]; then
      echo "nm did not find ${symbol} in $IDF_ELF." >&2
      exit 1
    fi
    addr="0x$(printf '%s' "$nm_line" | awk '{print $1}')"
    echo "${symbol}: $nm_line"
    if ! ss_idf_probe_in_psram "$addr"; then
      echo "${symbol} is NOT in PSRAM ($addr). Expected 0x3c.... Internal DRAM is 0x3fc....." >&2
      exit 1
    fi
  done

  if [[ ! -f "$IDF_IMAGE" ]]; then
    echo "$IDF_IMAGE does not exist, so the OTA marker could not be verified." >&2
    echo "Do not upload an unverified image over Wi-Fi." >&2
    exit 1
  fi
  echo "Image OTA identity:"
  node "$SS_CLI_ROOT/scripts/image_tag.js" "$IDF_IMAGE" --expect-arch "$SHOTSTOPPER_ARCH"
}

#!/usr/bin/env bash
# Shared ESP-IDF helpers. Source after shotstopper_board.sh and
# shotstopper_cli.sh; do not execute this file directly.
#
# Production images still come from ./scripts/build (arduino-cli). This
# pipeline writes to build-idf/<arquitectura> and never touches build/.

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
    echo "No se encontró ESP-IDF (idf.py / export.sh)." >&2
    echo "Instala 5.5.x y vuelve a ejecutar:" >&2
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
    echo "export.sh de ${idf_root} no dejó idf.py en PATH." >&2
    exit 127
  }
  if [[ "${SS_IDF_QUIET:-}" != "1" ]]; then
    echo "ESP-IDF: ${idf_root}"
  fi
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
  if [[ ! -f "$dest/src/ArduinoBLE.h" ]]; then
    echo "Clonando ArduinoBLE ${ARDUINO_BLE_VERSION} → $dest"
    mkdir -p "$(dirname "$dest")"
    git clone --depth 1 --branch "$ARDUINO_BLE_VERSION" "$ARDUINO_BLE_REPO" "$dest"
  fi
  # Scan 40/20, OOM-safe discover, and BLE host PSRAM (same as Arduino-CLI).
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
    echo "No existe $IDF_IMAGE." >&2
    echo "Compila primero: ./scripts/build-idf --arch $SHOTSTOPPER_ARCH" >&2
    exit 1
  fi
}

# ALLOW_BSS / autostart / g_probe, then the same OTA identity check as
# ./scripts/build (image_tag.js).
ss_idf_verify_firmware() {
  ss_idf_resolve_paths

  if [[ ! -f "$IDF_SDKCONFIG" ]]; then
    echo "No existe $IDF_SDKCONFIG; la compilación IDF no dejó sdkconfig." >&2
    exit 1
  fi
  if ! grep -q '^CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y$' "$IDF_SDKCONFIG"; then
    echo "ALLOW_BSS no está activo en $IDF_SDKCONFIG." >&2
    grep 'SPIRAM_ALLOW_BSS' "$IDF_SDKCONFIG" >&2 || true
    exit 1
  fi
  echo "sdkconfig: CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y"

  if ! grep -q '^CONFIG_AUTOSTART_ARDUINO=y$' "$IDF_SDKCONFIG"; then
    echo "CONFIG_AUTOSTART_ARDUINO no está activo en $IDF_SDKCONFIG." >&2
    exit 1
  fi
  echo "sdkconfig: CONFIG_AUTOSTART_ARDUINO=y"

  if [[ ! -f "$IDF_ELF" ]]; then
    echo "No existe $IDF_ELF." >&2
    exit 1
  fi
  command -v xtensa-esp32s3-elf-nm >/dev/null 2>&1 || {
    echo "No se encontró xtensa-esp32s3-elf-nm (¿export.sh?)." >&2
    exit 127
  }

  local nm_line addr
  nm_line="$(xtensa-esp32s3-elf-nm "$IDF_ELF" | grep -E '[[:space:]]g_probe$|[[:space:]]_ZL[0-9]+g_probe$' || true)"
  if [[ -z "$nm_line" ]]; then
    echo "nm no encontró g_probe en $IDF_ELF." >&2
    exit 1
  fi
  addr="0x$(printf '%s' "$nm_line" | awk '{print $1}')"
  echo "g_probe: $nm_line"

  if ss_idf_probe_in_psram "$addr"; then
    echo "BSS probe en PSRAM ($addr). ALLOW_BSS funciona en este toolchain."
  else
    echo "BSS probe NO está en PSRAM ($addr). Se esperaba 0x3c.... DRAM interna es 0x3fc....." >&2
    if [[ -f "$IDF_MAP" ]]; then
      echo "Extracto del .map:" >&2
      grep -n -A2 -E 'g_probe|ext_ram\.bss|extern_ram_seg' "$IDF_MAP" | head -40 >&2 || true
    fi
    exit 1
  fi

  if [[ ! -f "$IDF_IMAGE" ]]; then
    echo "No existe $IDF_IMAGE, así que no se pudo verificar el" >&2
    echo "marcador OTA. No subas por Wi-Fi una imagen sin verificar." >&2
    exit 1
  fi
  echo "Identidad OTA de la imagen:"
  node "$SS_CLI_ROOT/scripts/image_tag.js" "$IDF_IMAGE" --expect-arch "$SHOTSTOPPER_ARCH"
}

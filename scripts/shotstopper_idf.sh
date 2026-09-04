#!/usr/bin/env bash
# Shared ESP-IDF helpers. Source after shotstopper_board.sh and
# shotstopper_cli.sh; do not execute this file directly.
#
# Official firmware builds write to build-idf/<architecture> and use native
# ESP-IDF NimBLE as the only BLE backend.

IDF_PROJECT_NAME="shotstopper"
IDF_DEFAULT_HOME="${HOME}/esp/esp-idf"

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

# True when extra flags request USB Serial/JTAG at boot.
ss_idf_jtag_enabled() {
  local tok
  # Word-splitting is intentional: extra flags is a space-separated list.
  # shellcheck disable=SC2086
  set -- ${SHOT_STOPPER_EXTRA_FLAGS-}
  for tok in "$@"; do
    case "$tok" in
      -DSHOT_STOPPER_ENABLE_JTAG=1|SHOT_STOPPER_ENABLE_JTAG=1)
        return 0
        ;;
    esac
  done
  return 1
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
  IDF_SDKCONFIG_DEFAULTS="$IDF_PROJECT/sdkconfig.defaults;$IDF_PROJECT/sdkconfig.defaults.nimble;$IDF_PROJECT/sdkconfig.defaults.$SHOTSTOPPER_ARCH"
  if ss_idf_jtag_enabled; then
    IDF_SDKCONFIG_DEFAULTS="$IDF_SDKCONFIG_DEFAULTS;$IDF_PROJECT/sdkconfig.defaults.jtag"
  fi
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

# idf.py skips CMake when CMakeCache.txt exists, so a new
# SHOT_STOPPER_EXTRA_FLAGS env value would otherwise never reach the compiler.
# Drop the cache when the stamp changes. Do not write the stamp here:
# idf.py set-target runs fullclean, which refuses a non-empty directory that
# is not a CMake build (no CMakeCache.txt). Writing the stamp first made the
# first n16r8/n8r4 tree fail. Call ss_idf_commit_extra_flags_stamp after
# set-target. CMake still reads SHOT_STOPPER_EXTRA_FLAGS from the environment.
ss_idf_sync_extra_flags() {
  ss_idf_resolve_paths
  local stamp="$IDF_BUILD_DIR/shot_stopper_extra_flags"
  local new="${SHOT_STOPPER_EXTRA_FLAGS-}"
  local old=""
  mkdir -p "$IDF_BUILD_DIR"
  if [[ -f "$stamp" ]]; then
    old="$(cat "$stamp")"
  fi
  if [[ "$new" != "$old" ]]; then
    echo "Extra compile flags changed; reconfiguring IDF CMake cache"
    echo "  was: ${old:-(none)}"
    echo "  now: ${new:-(none)}"
    rm -f "$IDF_BUILD_DIR/CMakeCache.txt"
  fi
}

ss_idf_commit_extra_flags_stamp() {
  ss_idf_resolve_paths
  mkdir -p "$IDF_BUILD_DIR"
  printf '%s\n' "${SHOT_STOPPER_EXTRA_FLAGS-}" > "$IDF_BUILD_DIR/shot_stopper_extra_flags"
}

# Existing sdkconfig keeps CONSOLE_NONE vs USB_SERIAL_JTAG until rewritten.
# Drop it when the JTAG extra flag does not match so SDKCONFIG_DEFAULTS apply.
ss_idf_sync_jtag_console() {
  ss_idf_resolve_paths
  [[ -f "$IDF_SDKCONFIG" ]] || return 0
  local want=0 has=0
  ss_idf_jtag_enabled && want=1
  grep -q '^CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y$' "$IDF_SDKCONFIG" && has=1
  if [[ "$want" -eq "$has" ]]; then
    return 0
  fi
  echo "USB Serial/JTAG console mismatch (want=${want} has=${has}); dropping sdkconfig so defaults re-apply"
  rm -f "$IDF_SDKCONFIG" "$IDF_BUILD_DIR/CMakeCache.txt"
}

# sdkconfig.defaults only seed a new sdkconfig. Recreate an older build tree
# when it does not match the qualified production NimBLE profile; otherwise an
# incremental build could silently retain ArduinoBLE or A/B defaults.
ss_idf_sync_nimble_config() {
  ss_idf_resolve_paths
  [[ -f "$IDF_SDKCONFIG" ]] || return 0

  local stale=0 unused_service
  grep -q '^CONFIG_BT_NIMBLE_ENABLED=y$' "$IDF_SDKCONFIG" || stale=1
  grep -q '^CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y$' "$IDF_SDKCONFIG" || stale=1
  grep -q '^CONFIG_BT_CONTROLLER_ONLY=y$' "$IDF_SDKCONFIG" && stale=1
  for unused_service in PROX ANS CTS HTP IPSS TPS IAS LLS SPS HR BAS DIS; do
    if grep -q "^CONFIG_BT_NIMBLE_${unused_service}_SERVICE=y$" "$IDF_SDKCONFIG"; then
      stale=1
    fi
  done
  for unused_service in DTM_MODE_TEST SM_SIGN_CNT CPFD_CAFD; do
    if grep -q "^CONFIG_BT_NIMBLE_${unused_service}=y$" "$IDF_SDKCONFIG"; then
      stale=1
    fi
  done
  grep -q '^CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096$' "$IDF_SDKCONFIG" || stale=1
  if [[ "$stale" -eq 1 ]]; then
    echo "Native NimBLE production profile changed; recreating the IDF build configuration"
    rm -f "$IDF_SDKCONFIG" "$IDF_BUILD_DIR/CMakeCache.txt"
  fi
}

# idf.py set-target always fullcleans. fullclean is a no-op on an empty dir,
# but FatalError if the dir has leftover files and no CMakeCache.txt.
ss_idf_prepare_set_target() {
  ss_idf_resolve_paths
  if [[ -f "$IDF_BUILD_DIR/CMakeCache.txt" ]]; then
    return 0
  fi
  echo "Preparing empty IDF build tree for set-target"
  rm -rf "$IDF_BUILD_DIR"
  mkdir -p "$IDF_BUILD_DIR"
}

# Confirm each extra -D flag actually appears on ShotStopperNetwork.cpp.
ss_idf_verify_extra_flags() {
  ss_idf_resolve_paths
  local flags="${SHOT_STOPPER_EXTRA_FLAGS-}"
  local cc="$IDF_BUILD_DIR/compile_commands.json"
  [[ -n "$flags" ]] || return 0
  case " $flags " in
    *" -D"*) ;;
    *) return 0 ;;
  esac
  if [[ ! -f "$cc" ]]; then
    echo "$cc does not exist; cannot verify extra compile flags." >&2
    exit 1
  fi
  command -v python3 >/dev/null 2>&1 || {
    echo "python3 not found on PATH (needed to verify extra compile flags)." >&2
    exit 127
  }
  python3 - "$cc" "$flags" <<'PY'
import json, sys
cc_path, flags = sys.argv[1], sys.argv[2]
needles = [tok for tok in flags.split() if tok.startswith("-D")]
if not needles:
    sys.exit(0)
entries = json.load(open(cc_path))
cmd = None
for entry in entries:
    path = entry.get("file") or ""
    if path.endswith("ShotStopperNetwork.cpp"):
        cmd = entry.get("command") or " ".join(entry.get("arguments") or [])
        break
if not cmd:
    sys.stderr.write(
        "compile_commands.json has no ShotStopperNetwork.cpp entry.\n")
    sys.exit(1)
missing = [flag for flag in needles if flag not in cmd]
if missing:
    sys.stderr.write(
        "Extra -D flags did not reach the compiler "
        "(IDF CMake cache was probably configured with older flags):\n")
    for flag in missing:
        sys.stderr.write("  %s\n" % flag)
    sys.exit(1)
PY
  echo "compile_commands: extra -D flags present on ShotStopperNetwork.cpp"
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

  if ! grep -q '^CONFIG_SPIRAM_USE_MALLOC=y$' "$IDF_SDKCONFIG"; then
    echo "CONFIG_SPIRAM_USE_MALLOC is not enabled in $IDF_SDKCONFIG." >&2
    exit 1
  fi
  echo "sdkconfig: CONFIG_SPIRAM_USE_MALLOC=y"

  if ! grep -q '^CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y$' "$IDF_SDKCONFIG" ||
     ! grep -q '^CONFIG_LOG_MAXIMUM_LEVEL=4$' "$IDF_SDKCONFIG"; then
    echo "ESP-IDF log maximum must be DEBUG so the runtime selector can emit INFO/DEBUG." >&2
    grep 'CONFIG_LOG_MAXIMUM' "$IDF_SDKCONFIG" >&2 || true
    exit 1
  fi
  echo "sdkconfig: CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y"

  if ! grep -q '^CONFIG_AUTOSTART_ARDUINO=y$' "$IDF_SDKCONFIG"; then
    echo "CONFIG_AUTOSTART_ARDUINO is not enabled in $IDF_SDKCONFIG." >&2
    exit 1
  fi
  echo "sdkconfig: CONFIG_AUTOSTART_ARDUINO=y"

  if ! grep -q '^CONFIG_COMPILER_OPTIMIZATION_PERF=y$' "$IDF_SDKCONFIG"; then
    echo "CONFIG_COMPILER_OPTIMIZATION_PERF is not enabled in $IDF_SDKCONFIG." >&2
    echo "Delete that sdkconfig (or set Optimize for performance in menuconfig) and rebuild." >&2
    grep 'COMPILER_OPTIMIZATION' "$IDF_SDKCONFIG" >&2 || true
    exit 1
  fi
  echo "sdkconfig: CONFIG_COMPILER_OPTIMIZATION_PERF=y (-O2)"

  if ! grep -q '^CONFIG_BT_NIMBLE_ENABLED=y$' "$IDF_SDKCONFIG" ||
     grep -q '^CONFIG_BT_CONTROLLER_ONLY=y$' "$IDF_SDKCONFIG"; then
    echo "Native NimBLE host configuration is inconsistent in $IDF_SDKCONFIG." >&2
    grep -E 'CONFIG_(BT_NIMBLE_ENABLED|BT_CONTROLLER_ONLY)' "$IDF_SDKCONFIG" >&2 || true
    exit 1
  fi
  if ! grep -q '^CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y$' "$IDF_SDKCONFIG"; then
      echo "Production NimBLE must use the physically qualified external allocator." >&2
      exit 1
  fi
  local unused_service
  for unused_service in PROX ANS CTS HTP IPSS TPS IAS LLS SPS HR BAS DIS; do
    if grep -q "^CONFIG_BT_NIMBLE_${unused_service}_SERVICE=y$" "$IDF_SDKCONFIG"; then
      echo "Unused NimBLE ${unused_service} service is enabled in $IDF_SDKCONFIG." >&2
      exit 1
    fi
  done
  for unused_service in DTM_MODE_TEST SM_SIGN_CNT CPFD_CAFD; do
    if grep -q "^CONFIG_BT_NIMBLE_${unused_service}=y$" "$IDF_SDKCONFIG"; then
      echo "Unused NimBLE ${unused_service} feature is enabled in $IDF_SDKCONFIG." >&2
      exit 1
    fi
  done
  if ! grep -q '^CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096$' "$IDF_SDKCONFIG"; then
    echo "NimBLE host stack must remain at the qualified size (4096 bytes)." >&2
    exit 1
  fi
  if [[ -f "$IDF_MAP" ]] && grep -q 'libArduinoBLE.a(' "$IDF_MAP"; then
    echo "ArduinoBLE objects leaked into the production firmware map." >&2
    exit 1
  fi
  echo "BLE backend: native NimBLE host (external allocator); no ArduinoBLE objects in ELF map"

  if [[ ! -f "$IDF_ELF" ]]; then
    echo "$IDF_ELF does not exist." >&2
    exit 1
  fi
  command -v xtensa-esp32s3-elf-nm >/dev/null 2>&1 || {
    echo "xtensa-esp32s3-elf-nm not found (did you run export.sh?)." >&2
    exit 127
  }

  local nm_line addr ble_in_use_line
  ble_in_use_line="$(xtensa-esp32s3-elf-nm "$IDF_ELF" | grep -E '[[:space:]]T[[:space:]]+bleInUse$' || true)"
  if [[ -z "$ble_in_use_line" ]]; then
    echo "ELF does not contain the strong bleInUse override required to retain BLE controller memory." >&2
    exit 1
  fi
  echo "BLE memory retention: $ble_in_use_line"

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
  for symbol in shotLog shotCurves persistedSettings debugLog; do
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

  nm_line="$(xtensa-esp32s3-elf-nm "$IDF_ELF" | grep -E '[[:space:]][Tt][[:space:]]shotStopperScaleLog$' || true)"
  if [[ -z "$nm_line" ]]; then
    echo "The final ELF has no strong shotStopperScaleLog bridge definition." >&2
    exit 1
  fi
  echo "EspressoScaleBLE log bridge: $nm_line"

  if [[ ! -f "$IDF_IMAGE" ]]; then
    echo "$IDF_IMAGE does not exist, so the OTA marker could not be verified." >&2
    echo "Do not upload an unverified image over Wi-Fi." >&2
    exit 1
  fi
  echo "Image OTA identity:"
  node "$SS_CLI_ROOT/scripts/image_tag.js" "$IDF_IMAGE" --expect-arch "$SHOTSTOPPER_ARCH"

  if ss_idf_jtag_enabled; then
    if ! grep -q '^CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y$' "$IDF_SDKCONFIG"; then
      echo "SHOT_STOPPER_ENABLE_JTAG=1 but USB Serial/JTAG console is off in $IDF_SDKCONFIG." >&2
      grep 'CONFIG_ESP_CONSOLE' "$IDF_SDKCONFIG" >&2 || true
      exit 1
    fi
    echo "sdkconfig: CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y (JTAG build)"
  else
    if grep -q '^CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y$' "$IDF_SDKCONFIG" ||
       ! grep -q '^CONFIG_ESP_CONSOLE_NONE=y$' "$IDF_SDKCONFIG"; then
      echo "USB Serial/JTAG must stay off unless compiled with -DSHOT_STOPPER_ENABLE_JTAG=1." >&2
      grep 'CONFIG_ESP_CONSOLE' "$IDF_SDKCONFIG" >&2 || true
      exit 1
    fi
    echo "sdkconfig: CONFIG_ESP_CONSOLE_NONE=y (JTAG off)"
  fi

  ss_idf_verify_extra_flags
}

#!/usr/bin/env bash
# Shared ESP32-S3 board defaults and FQBN mapping. Source from other scripts;
# do not execute this file directly.

SHOTSTOPPER_DEFAULT_ARCH="n16r8"
SHOTSTOPPER_DEFAULT_PORT_HELP="primer /dev/cu.usbmodem<número> conectado"

# N8R4: 8 MB QSPI flash + 4 MB QSPI PSRAM. default_8MB is the normal 8 MB OTA
# table (two ~3.2 MB app slots).
# N16R8: 16 MB QSPI flash + 8 MB OPI PSRAM. app3M_fat9M_16MB is the normal
# 16 MB OTA table (two 3 MB app slots). min_spiffs is no longer used.
# USB CDC on boot matches native USB ports such as /dev/cu.usbmodem2101.
SHOTSTOPPER_FQBN_N8R4="esp32:esp32:esp32s3:PSRAM=enabled,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc"
SHOTSTOPPER_FQBN_N16R8="esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc"

shotstopper_arch_help() {
  cat <<'EOF'
Arquitecturas admitidas (solo ESP32-S3 con PSRAM):
  n8r4   ESP32-S3, 8 MB flash, 4 MB QSPI PSRAM
  n16r8  ESP32-S3, 16 MB flash, 8 MB OPI PSRAM (por defecto)

Alias: esp32s3 y esp32s3-n16r8 → n16r8; esp32s3-n8r4 → n8r4.
El ESP32 clásico y Nano ESP32 ya no están soportados.
EOF
}

# Prints the first /dev/cu.usbmodem<digits> device, in glob order.
# Returns 1 if none are present.
shotstopper_detect_port() {
  local p
  for p in /dev/cu.usbmodem[0-9]*; do
    [[ -e "$p" ]] || continue
    [[ "$p" =~ ^/dev/cu\.usbmodem[0-9]+$ ]] || continue
    printf '%s\n' "$p"
    return 0
  done
  return 1
}

# Sets SHOTSTOPPER_PORT. Pass "required" to fail if no matching device exists.
shotstopper_resolve_port() {
  local mode="${1:-optional}"
  local detected=""
  detected="$(shotstopper_detect_port)" || true
  if [[ -n "$detected" ]]; then
    SHOTSTOPPER_PORT="$detected"
    return 0
  fi
  if [[ "$mode" == "required" ]]; then
    echo "No se encontró ningún puerto /dev/cu.usbmodem<número>." >&2
    echo "Conecta el ESP32-S3 por USB CDC o indica la ruta serial." >&2
    return 1
  fi
  SHOTSTOPPER_PORT=""
  return 0
}

# Sets SHOTSTOPPER_ARCH, SHOTSTOPPER_FQBN, SHOTSTOPPER_BUILD_DIR.
shotstopper_resolve_board() {
  local arch="${1:-}"
  case "$arch" in
    n8r4|esp32s3-n8r4)
      SHOTSTOPPER_ARCH="n8r4"
      SHOTSTOPPER_FQBN="$SHOTSTOPPER_FQBN_N8R4"
      SHOTSTOPPER_BUILD_DIR="build/n8r4"
      ;;
    n16r8|esp32s3-n16r8|esp32s3)
      SHOTSTOPPER_ARCH="n16r8"
      SHOTSTOPPER_FQBN="$SHOTSTOPPER_FQBN_N16R8"
      SHOTSTOPPER_BUILD_DIR="build/n16r8"
      ;;
    esp32|esp32c3|nanoesp32|"")
      echo "Arquitectura no admitida: ${arch:-<vacía>}." >&2
      echo "Shot Stopper solo soporta ESP32-S3 con PSRAM (n8r4 o n16r8)." >&2
      return 2
      ;;
    *)
      echo "Arquitectura no admitida: $arch (usa n8r4 o n16r8)." >&2
      return 2
      ;;
  esac
}

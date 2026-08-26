#!/usr/bin/env bash
# Shared ESP32-S3 board table and FQBN mapping. Source from other scripts;
# do not execute this file directly.
#
# There is deliberately no default architecture and no automatic port pick.
# Every caller resolves those through scripts/shotstopper_cli.sh.

# N8R4: 8 MB QSPI flash + 4 MB QSPI PSRAM. default_8MB is the normal 8 MB OTA
# table (two ~3.2 MB app slots).
# N16R8: 16 MB QSPI flash + 8 MB OPI PSRAM. app3M_fat9M_16MB is the normal
# 16 MB OTA table (two 3 MB app slots). min_spiffs is no longer used.
# USB CDC on boot matches native USB ports such as /dev/cu.usbmodem2101.
SHOTSTOPPER_FQBN_N8R4="esp32:esp32:esp32s3:PSRAM=enabled,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc"
SHOTSTOPPER_FQBN_N16R8="esp32:esp32:esp32s3:PSRAM=opi,FlashMode=qio,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc"

# Usable bytes of a single OTA app slot, used by the OTA client for a local
# size pre-check before uploading anything to the controller.
SHOTSTOPPER_SLOT_BYTES_N8R4=3342336
SHOTSTOPPER_SLOT_BYTES_N16R8=3145728

shotstopper_arch_help() {
  cat <<'EOF'
Supported architectures (ESP32-S3 with PSRAM only):
  n8r4   ESP32-S3, 8 MB flash, 4 MB QSPI PSRAM
  n16r8  ESP32-S3, 16 MB flash, 8 MB OPI PSRAM

Aliases: esp32s3 and esp32s3-n16r8 → n16r8; esp32s3-n8r4 → n8r4.
Classic ESP32 and Nano ESP32 are no longer supported.
EOF
}

# Prints ESP32 USB-CDC serial devices, one per line, in glob order.
# macOS: /dev/cu.usbmodem<digits>  Linux: /dev/ttyACM<digits>
# Used to suggest a port at the interactive prompt; it never auto-selects.
shotstopper_detect_ports() {
  local p found=1
  for p in /dev/cu.usbmodem[0-9]*; do
    [[ -e "$p" ]] || continue
    [[ "$p" =~ ^/dev/cu\.usbmodem[0-9]+$ ]] || continue
    printf '%s\n' "$p"
    found=0
  done
  for p in /dev/ttyACM[0-9]*; do
    [[ -e "$p" ]] || continue
    [[ "$p" =~ ^/dev/ttyACM[0-9]+$ ]] || continue
    printf '%s\n' "$p"
    found=0
  done
  return $found
}

# Sets SHOTSTOPPER_ARCH, SHOTSTOPPER_FQBN, SHOTSTOPPER_BUILD_DIR,
# SHOTSTOPPER_SLOT_BYTES.
shotstopper_resolve_board() {
  local arch="${1:-}"
  case "$arch" in
    n8r4|esp32s3-n8r4)
      SHOTSTOPPER_ARCH="n8r4"
      SHOTSTOPPER_FQBN="$SHOTSTOPPER_FQBN_N8R4"
      SHOTSTOPPER_BUILD_DIR="build/n8r4"
      SHOTSTOPPER_SLOT_BYTES="$SHOTSTOPPER_SLOT_BYTES_N8R4"
      ;;
    n16r8|esp32s3-n16r8|esp32s3)
      SHOTSTOPPER_ARCH="n16r8"
      SHOTSTOPPER_FQBN="$SHOTSTOPPER_FQBN_N16R8"
      SHOTSTOPPER_BUILD_DIR="build/n16r8"
      SHOTSTOPPER_SLOT_BYTES="$SHOTSTOPPER_SLOT_BYTES_N16R8"
      ;;
    esp32|esp32c3|nanoesp32|"")
      echo "Unsupported architecture: ${arch:-<empty>}." >&2
      echo "Shot Stopper only supports ESP32-S3 with PSRAM (n8r4 or n16r8)." >&2
      return 2
      ;;
    *)
      echo "Unsupported architecture: $arch (use n8r4 or n16r8)." >&2
      return 2
      ;;
  esac
}

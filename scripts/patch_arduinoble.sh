#!/usr/bin/env bash
# Patch ArduinoBLE 2.1.0 for Shot Stopper:
#  1) GAP default idle 150/30 ms (0x00F0/0x0030); EspressoScaleBLE idle is
#     120/30 (25%) via setScanParameters, burst 60/30 (50%)
#  2) OOM-safe discovery (malloc + placement new; no abort on bad_alloc)
#  3) BLE host objects in PSRAM (GAP/ATT/GATT/local values)
#  4) Bound ATT indication / ACL credit waits (no indefinite blocks)
#  5) VHCI: 4 KiB RX/TX streams in internal DRAM (xStreamBufferCreateStatic),
#     non-blocking RX, TX without PDU drop, 4 KiB bleTask
#  6) Block BLE.poll(timeout) on VHCI RX instead of busy-spinning
#  7) BLEDevice copyAddress/copyLocalName (no Arduino String on the scan path)
#  8) Fixed 32-slot GAP BLEDevice pool in PSRAM BSS (no malloc per advert)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
oom_patch="$script_dir/../patches/ArduinoBLE-2.1.0-oom-safe-discover.patch"
psram_patch="$script_dir/../patches/ArduinoBLE-2.1.0-ble-host-psram.patch"
hci_patch="$script_dir/../patches/ArduinoBLE-2.1.0-hci-bounded-waits.patch"
vhci_init_patch="$script_dir/../patches/ArduinoBLE-2.1.0-vhci-controller-init.patch"
hci_wait_patch="$script_dir/../patches/ArduinoBLE-2.1.0-hci-blocking-wait.patch"
hci_nodrop_patch="$script_dir/../patches/ArduinoBLE-2.1.0-hci-nodrop.patch"
hci_static_patch="$script_dir/../patches/ArduinoBLE-2.1.0-hci-static-streams.patch"
gap_scan_patch="$script_dir/../patches/ArduinoBLE-2.1.0-gap-scan-params.patch"
copy_patch="$script_dir/../patches/ArduinoBLE-2.1.0-ble-device-copy.patch"
pool_patch="$script_dir/../patches/ArduinoBLE-2.1.0-gap-device-pool.patch"

candidates=()
if [[ -n "${ARDUINO_BLE_HOME:-}" ]]; then
  candidates+=("$ARDUINO_BLE_HOME")
fi
candidates+=(
  "$HOME/Documents/Arduino/libraries/ArduinoBLE"
  "$HOME/Arduino/libraries/ArduinoBLE"
)

if command -v arduino-cli >/dev/null 2>&1; then
  lib_dir="$(arduino-cli config dump 2>/dev/null | awk '
    $1 == "user:" { gsub("\r", "", $2); user=$2 }
    $1 == "directories:" { in_dir=1 }
    in_dir && $1 == "user:" { gsub("\r", "", $2); user=$2 }
    in_dir && $1 == "data:" { next }
    END { if (user != "") print user }
  ')"
  if [[ -n "${lib_dir:-}" ]]; then
    candidates+=("$lib_dir/libraries/ArduinoBLE")
  fi
fi

target=""
for candidate in "${candidates[@]}"; do
  if [[ -f "$candidate/src/utility/GAP.cpp" ]]; then
    target="$candidate"
    break
  fi
done

if [[ -z "$target" ]]; then
  echo "ArduinoBLE not found. Install ArduinoBLE@2.1.0 or set ARDUINO_BLE_HOME." >&2
  exit 1
fi

gap="$target/src/utility/GAP.cpp"
list="$target/src/utility/BLELinkedList.h"
host_alloc_h="$target/src/utility/BLEHostAlloc.h"
device_h="$target/src/BLEDevice.h"
vhci="$target/src/utility/HCIVirtualTransport.cpp"

restore_scan=0
apply_oom=0
apply_psram=0
apply_hci=0
apply_vhci_init=0
apply_hci_wait=0
apply_hci_nodrop=0
apply_hci_static=0
apply_gap_scan=0
apply_copy=0
apply_pool=0

if grep -q '_scanInterval(0x00F0)' "$gap"; then
  restore_scan=0
elif grep -q 'leSetScanParameters(0x01, 0x0020, 0x0020' "$gap"; then
  apply_gap_scan=1
elif grep -qE 'leSetScanParameters\(0x01, 0x0040, 0x0020|leSetScanParameters\(0x0[01], 0x00A0, 0x0030' "$gap"; then
  restore_scan=1
  apply_gap_scan=1
else
  echo "ArduinoBLE GAP scan params are not idle 150/30 default, stock 20/20, or a known leftover: $gap" >&2
  exit 1
fi

# OOM-safe discover may still use malloc, or already be upgraded to BLEHostAlloc
# or the fixed GAP device pool.
if ! grep -qE 'malloc\(sizeof\(BLEDevice\)\)|BLEHostAlloc\(sizeof\(BLEDevice\)\)|gapAllocDevice' "$gap" || \
   ! grep -qE 'malloc\(sizeof\(BLELinkedListNode|BLEHostAlloc\(sizeof\(BLELinkedListNode' "$list"; then
  apply_oom=1
fi

if [[ ! -f "$host_alloc_h" ]] || \
   { ! grep -q 'BLEHostAlloc(sizeof(BLEDevice))' "$gap" && \
     ! grep -q 'gapAllocDevice' "$gap"; }; then
  apply_psram=1
fi

if [[ ! -f "$vhci" ]] || ! grep -q 'HCI_VHCI_IO_TIMEOUT_MS' "$vhci"; then
  apply_hci=1
fi

if [[ -f "$vhci" ]] && grep -q 'HCI_VHCI_IO_TIMEOUT_MS' "$vhci" && \
   ! grep -q 'HCI_VHCI_TASK_STACK' "$vhci"; then
  apply_vhci_init=1
fi

if [[ -f "$vhci" ]] && grep -q 'HCI_VHCI_TASK_STACK' "$vhci" && \
   ! grep -q 'hci_wait_task' "$vhci"; then
  apply_hci_wait=1
fi

if [[ -f "$vhci" ]] && ! grep -q 'HCI_VHCI_STREAM_BYTES' "$vhci"; then
  apply_hci_nodrop=1
fi

if [[ -f "$vhci" ]] && grep -q 'HCI_VHCI_STREAM_BYTES' "$vhci" && \
   ! grep -q 'xStreamBufferCreateStatic' "$vhci"; then
  apply_hci_static=1
fi

if [[ -f "$gap" ]] && ! grep -q 'setScanParameters' "$gap"; then
  apply_gap_scan=1
fi

if [[ -f "$device_h" ]] && ! grep -q 'copyAddress' "$device_h"; then
  apply_copy=1
fi

if [[ -f "$gap" ]] && grep -q 'BLEHostFree(device)' "$gap" && \
   ! grep -q 'gapAllocDevice' "$gap"; then
  apply_pool=1
fi

if [[ "$restore_scan" -eq 0 && "$apply_oom" -eq 0 && "$apply_psram" -eq 0 && \
      "$apply_hci" -eq 0 && "$apply_vhci_init" -eq 0 && \
      "$apply_hci_wait" -eq 0 && "$apply_hci_nodrop" -eq 0 && \
      "$apply_hci_static" -eq 0 && \
      "$apply_gap_scan" -eq 0 && "$apply_copy" -eq 0 && \
      "$apply_pool" -eq 0 ]]; then
  echo "ArduinoBLE already patched (GAP idle default 150/30; EspressoScaleBLE idle 120/30 via setScanParameters + OOM-safe + host PSRAM + HCI no-drop + static VHCI streams + VHCI init + blocking HCI wait + copyAddress + GAP device pool): $target"
  exit 0
fi

if [[ "$restore_scan" -eq 1 ]]; then
  python3 - "$gap" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
old = text
replacements = (
    ("leSetScanParameters(0x01, 0x0040, 0x0020",
     "leSetScanParameters(0x01, 0x0020, 0x0020"),
    ("leSetScanParameters(0x00, 0x00A0, 0x0030",
     "leSetScanParameters(0x01, 0x0020, 0x0020"),
    ("leSetScanParameters(0x01, 0x00A0, 0x0030",
     "leSetScanParameters(0x01, 0x0020, 0x0020"),
)
for src, dst in replacements:
    if src in text:
        text = text.replace(src, dst, 1)
        break
if text == old:
    raise SystemExit("ArduinoBLE GAP scan params were not a known leftover 40/20 or 100/30 variant")
path.write_text(text)
PY
  echo "Normalized leftover ArduinoBLE GAP scan params toward stock 20/20 (gap-scan-params applies next): $target"
fi

if [[ "$apply_oom" -eq 1 ]]; then
  if [[ ! -f "$oom_patch" ]]; then
    echo "Missing OOM-safe patch: $oom_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $oom_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$oom_patch" >/dev/null 2>&1; then
    echo "OOM-safe discover patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, then re-run this script." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$oom_patch"
  echo "Patched ArduinoBLE OOM-safe discovery: $target"
fi

if [[ "$apply_psram" -eq 1 ]]; then
  if [[ ! -f "$psram_patch" ]]; then
    echo "Missing BLE host PSRAM patch: $psram_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $psram_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$psram_patch" >/dev/null 2>&1; then
    echo "BLE host PSRAM patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, run this script for OOM-safe discover, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$psram_patch"
  echo "Patched ArduinoBLE BLE host PSRAM allocator: $target"
  if [[ -f "$gap" ]] && grep -q 'BLEHostFree(device)' "$gap" && \
     ! grep -q 'gapAllocDevice' "$gap"; then
    apply_pool=1
  fi
fi

if [[ "$apply_hci" -eq 1 ]]; then
  if [[ ! -f "$hci_patch" ]]; then
    echo "Missing HCI bounded-waits patch: $hci_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $hci_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$hci_patch" >/dev/null 2>&1; then
    echo "HCI bounded-waits patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, apply prior patches, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$hci_patch"
  echo "Patched ArduinoBLE HCI bounded waits: $target"
  if [[ -f "$vhci" ]] && grep -q 'HCI_VHCI_IO_TIMEOUT_MS' "$vhci" && \
     ! grep -q 'HCI_VHCI_TASK_STACK' "$vhci"; then
    apply_vhci_init=1
  fi
fi

if [[ "$apply_vhci_init" -eq 1 ]]; then
  if [[ ! -f "$vhci_init_patch" ]]; then
    echo "Missing VHCI controller-init patch: $vhci_init_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $vhci_init_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$vhci_init_patch" >/dev/null 2>&1; then
    echo "VHCI controller-init patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, apply prior patches, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$vhci_init_patch"
  echo "Patched ArduinoBLE VHCI controller init: $target"
  if [[ -f "$vhci" ]] && grep -q 'HCI_VHCI_TASK_STACK' "$vhci" && \
     ! grep -q 'hci_wait_task' "$vhci"; then
    apply_hci_wait=1
  fi
fi

if [[ "$apply_hci_wait" -eq 1 ]]; then
  if [[ ! -f "$hci_wait_patch" ]]; then
    echo "Missing HCI blocking-wait patch: $hci_wait_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $hci_wait_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$hci_wait_patch" >/dev/null 2>&1; then
    echo "HCI blocking-wait patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, apply prior patches, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$hci_wait_patch"
  echo "Patched ArduinoBLE HCI blocking wait: $target"
fi

if [[ "$apply_hci_nodrop" -eq 1 ]]; then
  if [[ ! -f "$hci_nodrop_patch" ]]; then
    echo "Missing HCI no-drop patch: $hci_nodrop_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $hci_nodrop_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$hci_nodrop_patch" >/dev/null 2>&1; then
    echo "HCI no-drop patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, apply bounded-waits + VHCI init + blocking-wait, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$hci_nodrop_patch"
  echo "Patched ArduinoBLE HCI no-drop VHCI: $target"
  if [[ -f "$vhci" ]] && grep -q 'HCI_VHCI_STREAM_BYTES' "$vhci" && \
     ! grep -q 'xStreamBufferCreateStatic' "$vhci"; then
    apply_hci_static=1
  fi
fi

if [[ "$apply_hci_static" -eq 1 ]]; then
  if [[ ! -f "$hci_static_patch" ]]; then
    echo "Missing HCI static-stream patch: $hci_static_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $hci_static_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$hci_static_patch" >/dev/null 2>&1; then
    echo "HCI static-stream patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, apply no-drop VHCI, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$hci_static_patch"
  echo "Patched ArduinoBLE HCI static VHCI streams (internal DRAM): $target"
fi

if [[ "$apply_gap_scan" -eq 1 ]]; then
  if [[ ! -f "$gap_scan_patch" ]]; then
    echo "Missing GAP scan-params patch: $gap_scan_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $gap_scan_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$gap_scan_patch" >/dev/null 2>&1; then
    echo "GAP scan-params patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, apply prior patches, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$gap_scan_patch"
  echo "Patched ArduinoBLE GAP idle default 150/30 + setScanParameters (EspressoScaleBLE idle 120/30): $target"
fi

if [[ "$apply_copy" -eq 1 ]]; then
  if [[ ! -f "$copy_patch" ]]; then
    echo "Missing BLEDevice copy patch: $copy_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $copy_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$copy_patch" >/dev/null 2>&1; then
    echo "BLEDevice copyAddress patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, apply prior patches, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$copy_patch"
  echo "Patched ArduinoBLE BLEDevice copyAddress/copyLocalName: $target"
fi

if [[ "$apply_pool" -eq 1 ]]; then
  if [[ ! -f "$pool_patch" ]]; then
    echo "Missing GAP device-pool patch: $pool_patch" >&2
    exit 1
  fi
  if ! command -v patch >/dev/null 2>&1; then
    echo "patch(1) is required to apply $pool_patch" >&2
    exit 1
  fi
  if ! patch -p1 --dry-run -d "$target" < "$pool_patch" >/dev/null 2>&1; then
    echo "GAP device-pool patch does not apply cleanly to: $target" >&2
    echo "Install stock ArduinoBLE@2.1.0, apply host PSRAM, then retry." >&2
    exit 1
  fi
  patch -p1 -d "$target" < "$pool_patch"
  echo "Patched ArduinoBLE GAP BLEDevice pool: $target"
fi

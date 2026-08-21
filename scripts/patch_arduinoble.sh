#!/usr/bin/env bash
# Patch ArduinoBLE 2.1.0 for Shot Stopper:
#  1) Keep GAP scan at stock active 20/20 ms (revert leftover 40/20 or 100/30)
#  2) OOM-safe discovery (malloc + placement new; no abort on bad_alloc)
#  3) BLE host objects in PSRAM (GAP/ATT/GATT/local values; VHCI stream buffers untouched)
#  4) Bound ESP32 VHCI / HCI ACL / ATT indication waits (no indefinite blocks)
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
oom_patch="$script_dir/../patches/ArduinoBLE-2.1.0-oom-safe-discover.patch"
psram_patch="$script_dir/../patches/ArduinoBLE-2.1.0-ble-host-psram.patch"
hci_patch="$script_dir/../patches/ArduinoBLE-2.1.0-hci-bounded-waits.patch"

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
vhci="$target/src/utility/HCIVirtualTransport.cpp"

restore_scan=0
apply_oom=0
apply_psram=0
apply_hci=0

if grep -q 'leSetScanParameters(0x01, 0x0020, 0x0020' "$gap"; then
  restore_scan=0
elif grep -qE 'leSetScanParameters\(0x01, 0x0040, 0x0020|leSetScanParameters\(0x0[01], 0x00A0, 0x0030' "$gap"; then
  restore_scan=1
else
  echo "ArduinoBLE GAP scan params are not stock 20/20 or a known leftover 40/20 / 100/30 variant: $gap" >&2
  exit 1
fi

# OOM-safe discover may still use malloc, or already be upgraded to BLEHostAlloc.
if ! grep -qE 'malloc\(sizeof\(BLEDevice\)\)|BLEHostAlloc\(sizeof\(BLEDevice\)\)' "$gap" || \
   ! grep -qE 'malloc\(sizeof\(BLELinkedListNode|BLEHostAlloc\(sizeof\(BLELinkedListNode' "$list"; then
  apply_oom=1
fi

if [[ ! -f "$host_alloc_h" ]] || ! grep -q 'BLEHostAlloc(sizeof(BLEDevice))' "$gap"; then
  apply_psram=1
fi

if [[ ! -f "$vhci" ]] || ! grep -q 'HCI_VHCI_IO_TIMEOUT_MS' "$vhci"; then
  apply_hci=1
fi

if [[ "$restore_scan" -eq 0 && "$apply_oom" -eq 0 && "$apply_psram" -eq 0 && "$apply_hci" -eq 0 ]]; then
  echo "ArduinoBLE already patched (GAP scan stock 20/20 + OOM-safe + host PSRAM + HCI waits): $target"
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
comment_replacements = (
    ("// Active scan, 40 ms interval / 20 ms window (N * 0.625). Duty 50%. Active is\n"
     "  // required: scale names usually live in SCAN_RSP, and ArduinoBLE only reports\n"
     "  // a device as discovered after type 0x03/0x04.",
     "// active scan, 20 ms scan interval (N * 0.625), 20 ms scan window (N * 0.625), public own address type, no filter"),
    ("// Active scan, 40 ms interval / 20 ms window (N * 0.625). Duty 50%. Active is required:",
     "// active scan, 20 ms scan interval (N * 0.625), 20 ms scan window (N * 0.625), public own address type, no filter"),
)
for src, dst in comment_replacements:
    if src in text:
        text = text.replace(src, dst, 1)
        break
if text == old:
    raise SystemExit("ArduinoBLE GAP scan params were not a known leftover 40/20 or 100/30 variant")
path.write_text(text)
PY
  echo "Restored ArduinoBLE GAP scan to stock active 20/20: $target"
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
fi

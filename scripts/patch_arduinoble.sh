#!/usr/bin/env bash
# Patch ArduinoBLE 2.1.0 GAP scan to active 40/20 ms (50% duty).
# Stock is active 20/20. Also converts a leftover 100/30 (active or passive).
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
patch_file="$script_dir/../patches/ArduinoBLE-2.1.0-scan-40-20.patch"

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

if grep -q 'leSetScanParameters(0x01, 0x0040, 0x0020' "$gap"; then
  echo "ArduinoBLE GAP scan already 40/20: $target"
  exit 0
fi

if [[ -f "$patch_file" ]] && command -v patch >/dev/null 2>&1; then
  if patch -p1 --dry-run -d "$target" < "$patch_file" >/dev/null 2>&1; then
    patch -p1 -d "$target" < "$patch_file"
    echo "Patched ArduinoBLE GAP scan to active 40/20: $target"
    exit 0
  fi
fi

python3 - "$gap" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
text = path.read_text()
old = text
replacements = (
    ("leSetScanParameters(0x01, 0x0020, 0x0020",
     "leSetScanParameters(0x01, 0x0040, 0x0020"),
    ("leSetScanParameters(0x00, 0x00A0, 0x0030",
     "leSetScanParameters(0x01, 0x0040, 0x0020"),
    ("leSetScanParameters(0x01, 0x00A0, 0x0030",
     "leSetScanParameters(0x01, 0x0040, 0x0020"),
)
for src, dst in replacements:
    if src in text:
        text = text.replace(src, dst, 1)
        break
comment_replacements = (
    ("// active scan, 20 ms scan interval (N * 0.625), 20 ms scan window (N * 0.625), public own address type, no filter",
     "// Active scan, 40 ms interval / 20 ms window (N * 0.625). Duty 50%. Active is\n"
     "  // required: scale names usually live in SCAN_RSP, and ArduinoBLE only reports\n"
     "  // a device as discovered after type 0x03/0x04."),
    ("// Active scan, 20 ms interval / 20 ms window (N * 0.625). Active is required:",
     "// Active scan, 40 ms interval / 20 ms window (N * 0.625). Duty 50%. Active is required:"),
)
for src, dst in comment_replacements:
    if src in text:
        text = text.replace(src, dst, 1)
        break
if text == old:
    raise SystemExit("ArduinoBLE GAP scan params were not a known 20/20 or 100/30 variant")
path.write_text(text)
PY
echo "Patched ArduinoBLE GAP scan to active 40/20: $target"

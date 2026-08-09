#!/bin/sh
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_binary=${TMPDIR:-/tmp}/shot_stopper_host_test
sanitized_binary=${TMPDIR:-/tmp}/shot_stopper_host_test_sanitized
persistence_binary=${TMPDIR:-/tmp}/shot_stopper_persistence_host_test
persistence_sanitized=${TMPDIR:-/tmp}/shot_stopper_persistence_host_test_sanitized
firmware_file="$test_dir/../shotStopper.ino"
cxx=${CXX:-c++}

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  "$test_dir/shot_stopper_host_test.cpp" \
  -o "$test_binary"

"$test_binary"

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fno-omit-frame-pointer -fsanitize=address,undefined \
  "$test_dir/shot_stopper_host_test.cpp" \
  -o "$sanitized_binary"

ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$sanitized_binary"

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  "$test_dir/persistence_host_test.cpp" \
  -o "$persistence_binary"
"$persistence_binary"

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fno-omit-frame-pointer -fsanitize=address,undefined \
  "$test_dir/persistence_host_test.cpp" \
  -o "$persistence_sanitized"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$persistence_sanitized"

for removed_symbol in MOMENTARY REEDSWITCH REED_IN BUTTON_STATE_ARRAY_LENGTH; do
  if grep -n "$removed_symbol" "$firmware_file"; then
    echo "Removed legacy symbol remains in firmware: $removed_symbol" >&2
    exit 1
  fi
done

echo "Legacy Micra-incompatible paths: absent"

for removed_ble_config in 'BLEService weightService' \
  'BLEByteCharacteristic weightCharacteristic' '0x0FFE' '0xFF11' \
  PUBLISH_GOAL GOAL_UPDATE processGoalWeightUpdate; do
  if grep -n "$removed_ble_config" "$firmware_file"; then
    echo "Removed BLE configuration remains in firmware: $removed_ble_config" >&2
    exit 1
  fi
done

echo "BLE configuration peripheral: absent"

if command -v node >/dev/null 2>&1; then
  node "$test_dir/check_web_assets.js"
else
  echo "Node.js not found: embedded Web UI syntax check skipped" >&2
fi

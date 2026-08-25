#!/bin/sh
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$test_dir/../.." && pwd)
# No board arch: host tests never produce a flashable image, and the resulting
# "unknown" arch is rejected by the controller's OTA verifier by construction.
"$repo_root/scripts/gen_version.sh"
test_binary=${TMPDIR:-/tmp}/shot_stopper_host_test
sanitized_binary=${TMPDIR:-/tmp}/shot_stopper_host_test_sanitized
persistence_binary=${TMPDIR:-/tmp}/shot_stopper_persistence_host_test
persistence_sanitized=${TMPDIR:-/tmp}/shot_stopper_persistence_host_test_sanitized
external_safety_binary=${TMPDIR:-/tmp}/shot_stopper_external_safety_host_test
external_safety_sanitized=${TMPDIR:-/tmp}/shot_stopper_external_safety_host_test_sanitized
remote_policy_binary=${TMPDIR:-/tmp}/shot_stopper_remote_policy_host_test
ota_image_binary=${TMPDIR:-/tmp}/shot_stopper_ota_image_host_test
ota_image_sanitized=${TMPDIR:-/tmp}/shot_stopper_ota_image_host_test_sanitized
firmware_dir="$test_dir/.."
firmware_file="$firmware_dir/shotStopper.cpp"
ble_companion_file="$firmware_dir/ShotStopperBleCompanion.h"
cxx=${CXX:-c++}

# Domain split moved paddle/machine circuit/BBW out of shotStopper.cpp. Forbidden-symbol
# greps must cover those headers or a regression there would miss CI.
scan_firmware_sources() {
  grep -n "$1" \
    "$firmware_file" \
    "$firmware_dir/ShotStopperHardware.h" \
    "$firmware_dir/ShotStopperMachine.h" \
    "$firmware_dir/ShotStopperMachineRelay.h" \
    "$firmware_dir/ShotStopperMachineActivatorSample.h" \
    "$firmware_dir/ShotStopperMachinePaddleInput.h" \
    "$firmware_dir/ShotStopperMachinePaddleControl.h" \
    "$firmware_dir/ShotStopperMachinePaddleState.h" \
    "$firmware_dir/ShotStopperMachinePaddlePolicy.h" \
    "$firmware_dir/ShotStopperMachinePaddleConfig.h" \
    "$firmware_dir/ShotStopperMachineMomentaryInput.h" \
    "$firmware_dir/ShotStopperMachineMomentaryConfig.h" \
    "$firmware_dir/ShotStopperMachineMomentaryControl.h" \
    "$firmware_dir/ShotStopperMachineMomentaryReedState.h" \
    "$firmware_dir/ShotStopperMachineMomentaryOnlyState.h" \
    "$firmware_dir/ShotStopperMachineTypes.h" \
    "$firmware_dir/ShotStopperScaleSense.h" \
    "$firmware_dir/ShotStopperCupPresence.h" \
    "$firmware_dir/ShotStopperScaleTypes.h" \
    "$firmware_dir/ShotStopperBrew.h" \
    "$firmware_dir/ShotStopperBrewTypes.h" \
    "$firmware_dir/ShotStopperAlert.h" \
    "$firmware_dir/ShotStopperAlertChannel.h" \
    "$firmware_dir/ShotStopperAlertTone.h" \
    "$firmware_dir/ShotStopperBuzzer.h" \
    "$firmware_dir/ShotStopperBuzzerPatterns.h" \
    "$firmware_dir/ShotStopperBuzzerPassive.h" \
    "$firmware_dir/ShotStopperBuzzerRtttl.h"
}

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  "$test_dir/shot_stopper_host_test.cpp" \
  -o "$test_binary"

"$test_binary"

momentary_binary=${TMPDIR:-/tmp}/shot_stopper_momentary_host_test
for machine_type in 1 2; do
  "$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -DSHOT_STOPPER_MACHINE_TYPE="$machine_type" \
    "$test_dir/momentary_machine_host_test.cpp" \
    -o "$momentary_binary"
  "$momentary_binary"
  "$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -DSHOT_STOPPER_MACHINE_TYPE="$machine_type" \
    "$test_dir/shot_stopper_host_test.cpp" \
    -o /tmp/shot_stopper_host_test_type"$machine_type"
  echo "Momentary machine type $machine_type: host compile OK"
done

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

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  "$test_dir/safety_external_host_test.cpp" \
  -o "$external_safety_binary"
"$external_safety_binary"

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fno-omit-frame-pointer -fsanitize=address,undefined \
  "$test_dir/safety_external_host_test.cpp" \
  -o "$external_safety_sanitized"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$external_safety_sanitized"

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  "$test_dir/remote_policy_host_test.cpp" \
  -o "$remote_policy_binary"
"$remote_policy_binary"
echo "Remote machine control policy: disabled by default"

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  "$test_dir/ota_image_host_test.cpp" \
  -o "$ota_image_binary"
"$ota_image_binary"

"$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -fno-omit-frame-pointer -fsanitize=address,undefined \
  "$test_dir/ota_image_host_test.cpp" \
  -o "$ota_image_sanitized"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$ota_image_sanitized"

json_arena_binary=${TMPDIR:-/tmp}/shot_stopper_json_arena_host_test
json_arena_cflags=""
for cjson_include in /opt/homebrew/include/cjson /usr/local/include/cjson \
    /usr/include/cjson; do
  if [ -f "$cjson_include/cJSON.h" ]; then
    json_arena_cflags="-I$cjson_include"
    for cjson_lib in /opt/homebrew/lib /usr/local/lib /usr/lib; do
      if [ -f "$cjson_lib/libcjson.dylib" ] || [ -f "$cjson_lib/libcjson.so" ] ||
         [ -f "$cjson_lib/libcjson.a" ]; then
        json_arena_cflags="$json_arena_cflags -L$cjson_lib -lcjson"
        break
      fi
    done
    break
  fi
done
if [ -n "$json_arena_cflags" ]; then
  # shellcheck disable=SC2086
  "$cxx" -std=c++17 -Wall -Wextra -Werror -pedantic \
    $json_arena_cflags \
    "$test_dir/json_arena_host_test.cpp" \
    -o "$json_arena_binary"
  "$json_arena_binary"
else
  echo "libcjson not found: json arena host test skipped" >&2
fi

for removed_symbol in REEDSWITCH REED_IN BUTTON_STATE_ARRAY_LENGTH; do
  if scan_firmware_sources "$removed_symbol"; then
    echo "Removed legacy symbol remains in firmware: $removed_symbol" >&2
    exit 1
  fi
done

echo "Legacy Micra-incompatible paths: absent"

for machine_gpio_leak in 'readRawActivatorOn(' 'pinMode(RELAY_GPIO' \
    '#if SHOT_STOPPER_MACHINE_TYPE'; do
  if grep -n "$machine_gpio_leak" "$firmware_file"; then
    echo "Machine GPIO/type leak remains in shotStopper.cpp: $machine_gpio_leak" >&2
    exit 1
  fi
done

echo "shotStopper.cpp machine GPIO/type leaks: absent"

if grep -n -E 'machinePollIntention|getScaleLinkSnapshot|cupPresenceState\(|scaleAvailable\(' \
    "$firmware_dir/ShotStopperBrew.h"; then
  echo "Brew/guards must not poll machine, scale link, or cup presence" >&2
  exit 1
fi
if grep -n -E 'onFirstDropsDetected|notifyCupPresenceTare|holdCupPresenceTransitions' \
    "$firmware_dir/ShotStopperScaleSense.h"; then
  echo "Scale sense must not call brew or cup effect functions" >&2
  exit 1
fi
if grep -n 'session.active' \
    "$firmware_dir/ShotStopperMachineMomentaryInput.h" \
    "$firmware_dir/ShotStopperMachineMomentaryConfig.h" \
    "$firmware_dir/ShotStopperMachineMomentaryControl.h" \
    "$firmware_dir/ShotStopperMachineMomentaryOnlyState.h"; then
  echo "Momentary machine must not read session.active" >&2
  exit 1
fi
if grep -n 'currentWeight' \
    "$firmware_dir/ShotStopperMachineMomentaryOnlyState.h"; then
  echo "Momentary-only state must not read currentWeight" >&2
  exit 1
fi
if grep -n 'session.automaticEnabled' "$firmware_dir/ShotStopperMachineRelay.h"; then
  echo "Relay must not read session.automaticEnabled" >&2
  exit 1
fi

echo "Stopper orchestration leaks: absent"

for removed_led_path in LED_RED_PIN LED_BLUE_PIN LED_GREEN_PIN 'analogWrite(' \
    SHOT_STOPPER_ENABLE_ALED rgbLedWrite status_indicator WS2812; do
  if scan_firmware_sources "$removed_led_path"; then
    echo "Removed discrete RGB LED path remains in firmware: $removed_led_path" >&2
    exit 1
  fi
done

echo "Legacy three-channel RGB LED path: absent"

for required_ble_config in '00000000-0000-0000-0000-000000000FFE' \
  BLE_COMPANION_PROTOCOL_VERSION '00000000-0000-0000-0000-00000000FF10' \
  '00000000-0000-0000-0000-00000000FF11' '00000000-0000-0000-0000-00000000FF12' \
  '00000000-0000-0000-0000-00000000FF13' '00000000-0000-0000-0000-00000000FF14' \
  '00000000-0000-0000-0000-00000000FF15' '00000000-0000-0000-0000-00000000FF16' \
  '00000000-0000-0000-0000-00000000FF17' '00000000-0000-0000-0000-00000000FF18' \
  '00000000-0000-0000-0000-00000000FF19' '00000000-0000-0000-0000-00000000FF20' \
  '00000000-0000-0000-0000-00000000FF21' '00000000-0000-0000-0000-00000000FF22' \
  '00000000-0000-0000-0000-00000000FF23' '00000000-0000-0000-0000-00000000FF24' \
  '00000000-0000-0000-0000-00000000FF25' '00000000-0000-0000-0000-00000000FF26'; do
  if ! grep -n "$required_ble_config" "$ble_companion_file" >/dev/null; then
    echo "Required BLE Companion contract is missing: $required_ble_config" >&2
    exit 1
  fi
done

echo "BLE Companion v2 characteristics: present"

for script in "$repo_root"/scripts/*; do
  [ -f "$script" ] || continue
  case "$script" in
    *.js) continue ;;
  esac
  # BSD grep has no \| in a basic regex, so match the shebang with a case glob.
  case "$(head -n 1 "$script")" in
    '#!'*sh|'#!'*sh\ *) ;;
    *) continue ;;
  esac
  if ! bash -n "$script"; then
    echo "Shell syntax error in $script" >&2
    exit 1
  fi
done

# A command line is readable by every user on the machine through `ps`, for as
# long as the child runs. An OTA upload runs for minutes.
if grep -n -E '\-\-token[[:space:]]+"' "$repo_root"/scripts/* ; then
  echo "The OTA token must reach a child through the environment, not argv" >&2
  exit 1
fi

# bash 3.2, the system bash on macOS, treats "${array[@]}" on an empty array as
# an unbound variable and aborts the script under `set -u`.
if grep -n 'SS_CLI_FORWARD\[@\]' "$repo_root"/scripts/* |
    grep -v 'SS_CLI_FORWARD\[@\]+' ; then
  echo 'Expand as ${SS_CLI_FORWARD[@]+"${SS_CLI_FORWARD[@]}"} for bash 3.2' >&2
  exit 1
fi

echo "Developer scripts: syntax OK, no secrets in argv, bash 3.2 safe arrays"

if command -v node >/dev/null 2>&1; then
  if [ ! -d "$repo_root/node_modules/terser" ]; then
    (CDPATH= cd -- "$repo_root" && npm install --no-fund --no-audit)
  fi
  node "$test_dir/check_web_assets.js"
else
  echo "Node.js not found: embedded Web UI syntax check skipped" >&2
fi

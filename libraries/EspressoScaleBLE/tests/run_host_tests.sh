#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/espresso-scale-ble-host-tests"
mkdir -p "$build_dir"

compiler="${CXX:-c++}"
common_flags=(
    -std=c++11
    -Wall
    -Wextra
    -Werror
    -Wdeprecated-copy
    -pedantic
    -I"$repo_root/tests/stubs"
    -I"$repo_root/src"
)

"$compiler" "${common_flags[@]}" \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  "$repo_root/src/EspressoScaleBLE.cpp" \
  "$repo_root/src/ScaleBleStateMachine.cpp" \
  "$repo_root/src/ScaleProtocolCommon.cpp" \
  "$repo_root/src/protocols/Registry.cpp" \
  "$repo_root/src/protocols/Acaia.cpp" \
  "$repo_root/src/protocols/GenericFf11.cpp" \
  "$repo_root/src/protocols/Felicita.cpp" \
  "$repo_root/src/protocols/Eclair.cpp" \
  "$repo_root/src/protocols/Decent.cpp" \
  "$repo_root/src/protocols/Difluid.cpp" \
  "$repo_root/src/protocols/Myscale.cpp" \
  "$repo_root/src/protocols/WeighMyBru.cpp" \
  "$repo_root/src/protocols/Varia.cpp" \
  "$repo_root/src/protocols/Eureka.cpp" \
  "$repo_root/tests/espresso_scale_ble_host_test.cpp" \
  -o "$build_dir/espresso_scale_ble_host_test"

ASAN_OPTIONS=detect_leaks=0 "$build_dir/espresso_scale_ble_host_test"

"$compiler" "${common_flags[@]}" \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  "$repo_root/src/ScaleBleStateMachine.cpp" \
  "$repo_root/tests/scale_ble_portable_test.cpp" \
  -o "$build_dir/scale_ble_portable_test"

ASAN_OPTIONS=detect_leaks=0 "$build_dir/scale_ble_portable_test"

"$compiler" "${common_flags[@]}" \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  "$repo_root/src/ScaleProtocolCommon.cpp" \
  "$repo_root/src/protocols/Registry.cpp" \
  "$repo_root/src/protocols/Acaia.cpp" \
  "$repo_root/src/protocols/GenericFf11.cpp" \
  "$repo_root/src/protocols/Felicita.cpp" \
  "$repo_root/src/protocols/Eclair.cpp" \
  "$repo_root/src/protocols/Decent.cpp" \
  "$repo_root/src/protocols/Difluid.cpp" \
  "$repo_root/src/protocols/Myscale.cpp" \
  "$repo_root/src/protocols/WeighMyBru.cpp" \
  "$repo_root/src/protocols/Varia.cpp" \
  "$repo_root/src/protocols/Eureka.cpp" \
  "$repo_root/src/nimble/NimbleAdvertisement.cpp" \
  "$repo_root/tests/nimble_advertisement_test.cpp" \
  -o "$build_dir/nimble_advertisement_test"

ASAN_OPTIONS=detect_leaks=0 "$build_dir/nimble_advertisement_test"

"$compiler" "${common_flags[@]}" \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  "$repo_root/src/nimble/NimbleResilience.cpp" \
  "$repo_root/tests/nimble_resilience_test.cpp" \
  -o "$build_dir/nimble_resilience_test"

ASAN_OPTIONS=detect_leaks=0 "$build_dir/nimble_resilience_test"

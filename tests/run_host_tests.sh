#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/acaia-arduino-ble-host-tests"
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
  -I"$repo_root"
)

"$compiler" "${common_flags[@]}" \
  -fsanitize=address,undefined \
  -fno-omit-frame-pointer \
  "$repo_root/AcaiaArduinoBLE.cpp" \
  "$repo_root/tests/acaia_host_test.cpp" \
  -o "$build_dir/acaia_host_test"

ASAN_OPTIONS=detect_leaks=0 "$build_dir/acaia_host_test"

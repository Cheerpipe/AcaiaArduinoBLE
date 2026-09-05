#!/bin/sh
set -eu

test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
firmware_file="$test_dir/../shotStopper.cpp"
domain_file="$test_dir/../ShotStopperDomain.h"
coverage_binary=${TMPDIR:-/tmp}/shot_stopper_coverage
raw_profile=${TMPDIR:-/tmp}/shot_stopper_coverage.profraw
merged_profile=${TMPDIR:-/tmp}/shot_stopper_coverage.profdata
persistence_file="$test_dir/../ShotStopperPersistence.h"
persistence_binary=${TMPDIR:-/tmp}/shot_stopper_persistence_coverage
persistence_raw=${TMPDIR:-/tmp}/shot_stopper_persistence_coverage.profraw
persistence_profile=${TMPDIR:-/tmp}/shot_stopper_persistence_coverage.profdata
cxx=${CXX:-c++}

if command -v llvm-profdata >/dev/null 2>&1; then
  llvm_profdata=$(command -v llvm-profdata)
elif command -v xcrun >/dev/null 2>&1; then
  llvm_profdata=$(xcrun --find llvm-profdata)
else
  echo "llvm-profdata is required for coverage" >&2
  exit 1
fi

if command -v llvm-cov >/dev/null 2>&1; then
  llvm_cov=$(command -v llvm-cov)
elif command -v xcrun >/dev/null 2>&1; then
  llvm_cov=$(xcrun --find llvm-cov)
else
  echo "llvm-cov is required for coverage" >&2
  exit 1
fi

"$cxx" -std=c++17 -O0 -fprofile-instr-generate -fcoverage-mapping \
  -pthread \
  "$test_dir/shot_stopper_host_test.cpp" \
  -o "$coverage_binary"

LLVM_PROFILE_FILE="$raw_profile" "$coverage_binary"
"$llvm_profdata" merge -sparse "$raw_profile" -o "$merged_profile"
"$llvm_cov" report "$coverage_binary" \
  -instr-profile="$merged_profile" \
  "$firmware_file" "$domain_file"

"$cxx" -std=c++17 -O0 -fprofile-instr-generate -fcoverage-mapping \
  "$test_dir/persistence_host_test.cpp" \
  -o "$persistence_binary"
LLVM_PROFILE_FILE="$persistence_raw" "$persistence_binary"
"$llvm_profdata" merge -sparse "$persistence_raw" -o "$persistence_profile"
"$llvm_cov" report "$persistence_binary" \
  -instr-profile="$persistence_profile" \
  "$persistence_file"

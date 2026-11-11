#!/usr/bin/env bash
# Builds everything for the commit image, as `mayhem` in /mayhem:
#   - the project's gtest suite (normal flags) for mayhem/test.sh, plus a known-answer probe
#   - four sanitized libFuzzer harnesses and their -standalone reproducers
# nanoflann is header-only and its tests build offline (gtest ships in-tree), so nothing is fetched.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS SRC

cd "$SRC"

# 1) Test suite + known-answer probe, project's normal flags, in gitignored build-tests/ (upstream `build*`).
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
      -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS" \
      -DBUILD_TESTING=ON -DNANOFLANN_BUILD_TESTS=ON -DNANOFLANN_BUILD_EXAMPLES=OFF \
      -DNANOFLANN_USE_SYSTEM_GTEST=OFF
cmake --build build-tests -j"$MAYHEM_JOBS" --target nanoflann_unit_tests

mkdir -p build-fuzz
$CXX -std=c++17 -O2 $COVERAGE_FLAGS -I"$SRC/include" "$SRC/mayhem/kat_probe.cpp" -o "$SRC/build-tests/kat_probe"

# 2) Harnesses: compiled+linked with the sanitizers and DWARF<=3; once with libFuzzer, once with the
#    run-once driver (compiled as C so LLVMFuzzerTestOneInput keeps C linkage).
$CC  $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o build-fuzz/standalone_main.o
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o build-fuzz/lsan_off.o

CXXFLAGS_FUZZ="-std=c++17 -O1 -I$SRC/include -I$SRC/mayhem"
build_harness() {
  local h="$1"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $CXXFLAGS_FUZZ $LIB_FUZZING_ENGINE \
       "$SRC/mayhem/$h.cpp" build-fuzz/lsan_off.o -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $CXXFLAGS_FUZZ \
       "$SRC/mayhem/$h.cpp" build-fuzz/standalone_main.o build-fuzz/lsan_off.o -o "$SRC/$h-standalone"
}
export -f build_harness
export CXXFLAGS_FUZZ
printf '%s\n' fuzz_static_search fuzz_load_index fuzz_dynamic fuzz_incremental \
  | xargs -P "$MAYHEM_JOBS" -I{} bash -c 'build_harness {}'

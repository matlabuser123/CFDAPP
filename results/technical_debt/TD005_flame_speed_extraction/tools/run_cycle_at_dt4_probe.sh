#!/usr/bin/env bash
# TD-005 -- build and run the reproducibility probe, FAIL-CLOSED.
#
# TD-015 records that P18.4's probe cannot be rebuilt from its own evidence because its compile
# command was never recorded. This script IS that record: it is committed next to the probe, so the
# measurement is reproducible from the evidence directory alone.
#
# FAIL-CLOSED ORDER, which matters: CLAUDE.md section 8 and this campaign's own experience
# ("if [ -x binary ] after a failed build runs the STALE binary") require the library to be
# demonstrably rebuilt against the exact source being measured BEFORE anything runs.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD="$REPO/build/release"
TOOLS="$REPO/results/technical_debt/TD005_flame_speed_extraction/tools"
BIN=/tmp/td005_cycledt4

cd "$REPO" || exit 1

echo "=== ENVIRONMENT (CLAUDE.md hardware policy: record what was actually used)"
echo "date        $(date -Is)"
echo "nproc       $(nproc)"
echo "compiler    $(/usr/bin/c++ --version | head -1)"
echo "build type  $(grep -E '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo "openmp      $(grep -E '^CFDAPP_ENABLE_OPENMP' "$BUILD/CMakeCache.txt")"
echo "cuda        $(grep -E '^CFDAPP_ENABLE_CUDA' "$BUILD/CMakeCache.txt")"
echo

echo "=== REBUILD cfdcore against the current source"
cmake --build "$BUILD" --target cfdcore -j 20
rc=$?
echo "build exit: $rc"
if [ "$rc" -ne 0 ]; then
  echo "*** ABORT: the library did not build. No measurement is made, and no stale binary is run."
  exit 1
fi
echo

echo "=== PROVENANCE of what is being measured"
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$REPO/src/species/PremixedFlame.cpp" "$REPO/include/cfd/species/PremixedFlame.hpp"
echo "newest production source vs library:"
ls -l --time-style=full-iso "$BUILD/src/libcfdcore.a" | awk '{print "  lib  " $6, $7}'
ls -l --time-style=full-iso "$REPO/src/species/PremixedFlame.cpp" | awk '{print "  src  " $6, $7}'
echo

echo "=== COMPILE the probe (this command is the reproducibility record)"
CXXFLAGS="-I$REPO/include -I$BUILD/generated/include -I$BUILD/_deps/nlohmann_json-src/include"
CXXFLAGS="$CXXFLAGS -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic"
echo "/usr/bin/c++ $CXXFLAGS $TOOLS/cycle_at_dt4_probe.cpp -o $BIN $BUILD/src/libcfdcore.a"
# shellcheck disable=SC2086
/usr/bin/c++ $CXXFLAGS "$TOOLS/cycle_at_dt4_probe.cpp" -o "$BIN" "$BUILD/src/libcfdcore.a"
rc=$?
echo "compile exit: $rc"
if [ "$rc" -ne 0 ]; then
  echo "*** ABORT: the probe did not compile."
  exit 1
fi
echo

echo "=== RUN"
"$BIN"
rc=$?
echo
echo "probe exit: $rc"
exit "$rc"

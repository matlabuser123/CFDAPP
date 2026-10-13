#!/bin/bash
# TD-032 -- THE DRY RUN, against the UNCHANGED tree. Fails closed.
#
# This produces, in one run: C0's instrument self-test, C1's and C2's baseline values (which must
# FAIL -- the gate's fail-direction non-vacuity), and the raw numbers C3, C4, C5 and C7's thresholds
# are filled in from at freeze time. It is ALSO the C8 negative control: the unchanged tree is the
# lagged library, and its sha256 is recorded here so the post-fix run is demonstrably a different
# library.
#
# NO production file is touched. The gate is frozen only AFTER this run (CLAUDE.md 3, steps 5-8).
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD032_lagged_time_level
BIN=$ITEM/tools/td032_alternation

echo "=== ENVIRONMENT (CLAUDE.md hardware policy: record what was actually used)"
echo "date        $(date -Is)"
echo "nproc       $(nproc)"
echo "compiler    $(c++ --version | head -1)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo "openmp      $(grep -m1 '^CFDAPP_ENABLE_OPENMP' "$BUILD/CMakeCache.txt")"
echo "cuda        $(grep -m1 '^CFDAPP_ENABLE_CUDA' "$BUILD/CMakeCache.txt")"
free -h | sed 's/^/mem         /'
echo

echo "=== REBUILD cfdcore against the current source (regression integrity, CLAUDE.md 8)"
cmake --build "$BUILD" --target cfdcore -j 20
if [ $? -ne 0 ]; then
  echo "*** ABORT: the library did not build."
  exit 1
fi
echo "build exit: 0"
echo

echo "=== PROVENANCE -- this is the C8 negative-control library, the one WITH the lag"
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
sha256sum "$REPO/include/cfd/species/PremixedFlame.hpp"
sha256sum "$ITEM/GATE.md"
sha256sum "$ITEM/tools/alternation_probe.cpp"
echo "newest production source vs library:"
echo "  lib  $(stat -c '%y' "$BUILD/src/libcfdcore.a")"
echo "  src  $(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs stat -c '%y' | sort | tail -1)"
echo

echo "=== COMPILE the probe (this command is the reproducibility record)"
CMD="/usr/bin/c++ -I$REPO/include -I$BUILD/generated/include -I$BUILD/_deps/nlohmann_json-src/include -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic $ITEM/tools/alternation_probe.cpp -o $BIN $BUILD/src/libcfdcore.a"
echo "$CMD"
eval "$CMD"
if [ $? -ne 0 ]; then
  echo "*** ABORT: the probe did not compile."
  exit 1
fi
echo "compile exit: 0"
sha256sum "$BIN"
echo

echo "=== RUN  (expect roughly 1 hour: 5 solves at ~600 s plus the 250-cell fixture)"
"$BIN"
echo "probe exit: $?"
echo
echo "DONE $(date -Is)"

#!/bin/bash
# TD-032 -- DRY RUN, PASS 2, against the UNCHANGED tree. Sequential, never concurrent: the
# CLAUDE.md hardware policy gives an evidence-producing run the machine, and a ctest alongside a
# solve would corrupt both.
#
#   STAGE A   C3(b)'s baseline -- does the per-step change fall with march length on the lagged
#             tree? It must NOT, or C3(b) is vacuous.
#   STAGE B   C6's baseline -- the three suites, re-measured ON THIS TREE so the gate's expectation
#             is a measurement and not a citation of an earlier aggregate (CLAUDE.md 4, 8).
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD032_lagged_time_level
BIN=$ITEM/tools/td032_residual

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "nproc       $(nproc)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo

echo "=== PROVENANCE -- still the LAGGED library, unchanged since logs/01"
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
echo

# =============================================================== STAGE A
echo "=== STAGE A  C3(b) baseline: the per-step change against march length"
CMD="/usr/bin/c++ -I$REPO/include -I$BUILD/generated/include -I$BUILD/_deps/nlohmann_json-src/include -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic $ITEM/tools/residual_vs_length_probe.cpp -o $BIN $BUILD/src/libcfdcore.a"
echo "$CMD"
eval "$CMD"
if [ $? -ne 0 ]; then
  echo "*** ABORT: the probe did not compile."
  exit 1
fi
sha256sum "$BIN"
echo
"$BIN"
echo "stage A exit: $?"
echo

# =============================================================== STAGE B
echo "=== STAGE B  C6 baseline: the three suites, rebuilt then run"
echo
for t in CFDIoTests CFDAppLayerTests CFDSpeciesTests; do
  cmake --build "$BUILD" --target "$t" -j 20 >/dev/null 2>&1
  if [ $? -ne 0 ]; then
    echo "*** ABORT: $t did not build. A stale binary is not evidence (CLAUDE.md 8)."
    exit 1
  fi
done
echo "all three targets rebuilt against the current source"
echo

NEWEST=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs stat -c '%Y' | sort -n | tail -1)
for p in tests/unit/io/CFDIoTests tests/unit/app/CFDAppLayerTests tests/unit/species/CFDSpeciesTests; do
  B=$BUILD/$p
  T=$(stat -c '%Y' "$B")
  if [ "$T" -lt "$NEWEST" ]; then
    echo "*** ABORT: $p is OLDER than the newest production source. Fails closed."
    exit 1
  fi
  echo "ok  $p is newer than the newest production source   $(sha256sum "$B" | cut -c1-16)..."
done
echo

echo "--- CFDIoTests                 (expect 331)"
"$BUILD/tests/unit/io/CFDIoTests" 2>&1 | tail -4 | sed 's/^/    /'
echo
echo "--- CFDAppLayerTests           (expect 32)"
"$BUILD/tests/unit/app/CFDAppLayerTests" 2>&1 | tail -4 | sed 's/^/    /'
echo
echo "--- CFDSpeciesTests, excluding G8PremixedFlameTest   (expect 343)"
echo "    the exclusion is TD-004 territory and is carried from its baseline; it is recorded"
echo "    here as part of the invocation so the count is never quoted without its conditions."
"$BUILD/tests/unit/species/CFDSpeciesTests" --gtest_filter=-G8PremixedFlameTest.* 2>&1 | tail -4 | sed 's/^/    /'
echo
echo "DONE $(date -Is)"

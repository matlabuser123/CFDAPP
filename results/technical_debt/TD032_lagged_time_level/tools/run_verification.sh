#!/bin/bash
# TD-032 -- VERIFICATION against the FROZEN gate (GATE.md 52bc82554574e7b7...).
#
# Criteria are run in the frozen order. The two probe SOURCES are byte-identical to the ones frozen
# in logs/00_freeze.log -- only the library they link changes, which is the two-library attribution
# CLAUDE.md 6 requires. Both library hashes are printed so "before" and "after" are checkable
# against bytes.
#
#   STAGE A   rebuild, and PROVE the library actually changed
#   STAGE B   C0, C1, C2 + the raw numbers for C3(a), C4, C5, C7
#   STAGE C   C3(b)
#   STAGE D   C6
#
# Stages are sequential: the hardware policy gives an evidence-producing run the machine.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD032_lagged_time_level
LAGGED_LIB=3b94eb849f9c8277beded611db68c259b1b8d24e99f888af09a721f65da6e4b0
FROZEN_PROBE=6bf33900f7d56a4016e247b638ea0e8701f229347746c9c4ac5697d9b13cac82

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "nproc       $(nproc)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo

# =============================================================== STAGE A
echo "=== STAGE A  rebuild, and prove the library CHANGED"
cmake --build "$BUILD" --target cfdcore -j 20
if [ $? -ne 0 ]; then
  echo "*** ABORT: the fixed library did not build."
  exit 1
fi
NEW_LIB=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo
echo "lagged library (frozen)  $LAGGED_LIB"
echo "fixed  library (now)     $NEW_LIB"
if [ "$NEW_LIB" = "$LAGGED_LIB" ]; then
  echo "*** ABORT: THE LIBRARY DID NOT CHANGE. Either the fix did not land or the build is"
  echo "    stale. Stale-binary results are not evidence (CLAUDE.md 8). Fails closed."
  exit 1
fi
echo "the two libraries differ -- the comparison is between two real libraries"
echo
sha256sum "$REPO/src/species/PremixedFlame.cpp"
echo "  (pre-fix was dac2992006a51cce6ea9e9f497dc44aceac76026c8d1cc4405882cc38642e5a3)"
sha256sum "$REPO/include/cfd/species/PremixedFlame.hpp"
echo "  (pre-fix was d140cb6cafc9dac6889614fb8dcbbf70aa8b83ea1d564370c6aacd447fea728e --"
echo "   this one MUST be unchanged: the fix touches no header)"
echo
echo "--- the probe sources must be byte-identical to the frozen ones"
P1=$(sha256sum "$ITEM/tools/alternation_probe.cpp" | cut -d' ' -f1)
echo "alternation_probe.cpp  $P1"
if [ "$P1" != "$FROZEN_PROBE" ]; then
  echo "*** ABORT: the probe source changed after the freeze. A moved instrument invalidates"
  echo "    the comparison. Fails closed."
  exit 1
fi
echo "matches the frozen hash"
echo
echo "--- the test sources must be UNCHANGED by this item"
sha256sum "$REPO/tests/unit/species/test_premixed_flame.cpp"
sha256sum "$REPO/tests/unit/thermal/test_thermal_transient.cpp"
sha256sum "$REPO/tests/unit/transport/test_scalar_transport.cpp"
echo "  (compare against logs/00_freeze.log)"
echo

# =============================================================== STAGE B
echo "=== STAGE B  C0, C1, C2  (and the raw numbers for C3(a), C4, C5, C7)"
CMD="/usr/bin/c++ -I$REPO/include -I$BUILD/generated/include -I$BUILD/_deps/nlohmann_json-src/include -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic $ITEM/tools/alternation_probe.cpp -o $ITEM/tools/td032_alternation_fixed $BUILD/src/libcfdcore.a"
echo "$CMD"
eval "$CMD" || { echo "*** ABORT: probe did not compile."; exit 1; }
sha256sum "$ITEM/tools/td032_alternation_fixed"
echo
"$ITEM/tools/td032_alternation_fixed"
echo "stage B exit: $?"
echo
echo "NOTE ON C2's FIXTURE. The frozen gate names budgets 7256..7260. The probe derives them from"
echo "the fixture, and the derived budget scales as 1/S_L^2, so if S_L moved the derived budget"
echo "moves too. The determination line above reports what was actually used. If it is not"
echo "7256..7260 the run does NOT match the frozen fixture and that is a gate-execution deviation"
echo "to be recorded and resolved, NOT papered over."
echo

# =============================================================== STAGE C
echo "=== STAGE C  C3(b)"
CMD2="/usr/bin/c++ -I$REPO/include -I$BUILD/generated/include -I$BUILD/_deps/nlohmann_json-src/include -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic $ITEM/tools/residual_vs_length_probe.cpp -o $ITEM/tools/td032_residual_fixed $BUILD/src/libcfdcore.a"
eval "$CMD2" || { echo "*** ABORT: probe did not compile."; exit 1; }
sha256sum "$ITEM/tools/td032_residual_fixed"
echo
"$ITEM/tools/td032_residual_fixed"
echo "stage C exit: $?"
echo
echo "NOTE ON READING STAGE C. That probe's own verdict text was written for the BASELINE, where"
echo "'NO FALL' was the required result. Post-fix the required result is the OPPOSITE: the ratio"
echo "must be <= 0.50. Read the RATIO, not the probe's commentary."
echo

# =============================================================== STAGE D
echo "=== STAGE D  C6  the three suites, rebuilt then run"
for t in CFDIoTests CFDAppLayerTests CFDSpeciesTests; do
  cmake --build "$BUILD" --target "$t" -j 20 >/dev/null 2>&1 || {
    echo "*** ABORT: $t did not build."; exit 1; }
done
NEWEST=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs stat -c '%Y' | sort -n | tail -1)
for p in tests/unit/io/CFDIoTests tests/unit/app/CFDAppLayerTests tests/unit/species/CFDSpeciesTests; do
  B=$BUILD/$p
  if [ "$(stat -c '%Y' "$B")" -lt "$NEWEST" ]; then
    echo "*** ABORT: $p is OLDER than the newest production source. Fails closed."
    exit 1
  fi
  echo "ok  $p   $(sha256sum "$B" | cut -c1-16)..."
done
echo
echo "--- CFDIoTests                 (frozen expectation 331)"
"$BUILD/tests/unit/io/CFDIoTests" 2>&1 | tail -3 | sed 's/^/    /'
echo
echo "--- CFDAppLayerTests           (frozen expectation 32)"
"$BUILD/tests/unit/app/CFDAppLayerTests" 2>&1 | tail -3 | sed 's/^/    /'
echo
echo "--- CFDSpeciesTests, excluding G8PremixedFlameTest   (frozen expectation 343)"
"$BUILD/tests/unit/species/CFDSpeciesTests" --gtest_filter=-G8PremixedFlameTest.* 2>&1 | tail -3 | sed 's/^/    /'
echo
echo "DONE $(date -Is)"

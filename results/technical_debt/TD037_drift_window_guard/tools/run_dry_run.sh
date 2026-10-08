#!/bin/bash
# TD-037 -- the dry run, against the UNCHANGED tree. Fails closed.
#
# Produces in one run: C0's reproduction of the defect, C1's and C3's baseline values, the trial
# RECORD that C4 will be compared against verbatim, C2's baseline S_L, C5's baseline (the test
# TD-032 broke, expected to FAIL here) and C6's counts re-measured on this tree.
#
# It is ALSO the C8 negative control: the unchanged tree carries the defect, and its library sha256
# is recorded so the post-fix run is demonstrably a different library.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD037_drift_window_guard

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "nproc       $(nproc)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
free -h | sed 's/^/mem         /'
echo

echo "=== PROVENANCE -- this is C8's negative-control library, the one WITH the defect"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1
if [ $? -ne 0 ]; then
  echo "*** ABORT: the library did not build."
  exit 1
fi
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
sha256sum "$ITEM/GATE.md"
sha256sum "$ITEM/tools/ladder_probe.cpp"
echo

echo "=== COMPILE the probe"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/ladder_probe.cpp" \
  -o "$ITEM/tools/td037_ladder" "$BUILD/src/libcfdcore.a"
if [ $? -ne 0 ]; then
  echo "*** ABORT: the probe did not compile."
  exit 1
fi
sha256sum "$ITEM/tools/td037_ladder"
echo

echo "=== C0 / C1 / C3 / C4 -- the driftWindow ladder"
"$ITEM/tools/td037_ladder"
echo "probe exit: $?"
echo

echo "=== C5 -- the test TD-032 broke. EXPECTED TO FAIL on this tree."
cmake --build "$BUILD" --target CFDSpeciesTests -j 20 >/dev/null 2>&1
"$BUILD/tests/unit/species/CFDSpeciesTests" \
  --gtest_filter=G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial 2>&1 \
  | grep -E "RUN|OK|FAILED|drift-window spread|Failure|Which is" | sed 's|^|    |'
echo

echo "=== C6 -- regression outside the flame, re-measured on THIS tree"
for t in CFDIoTests CFDAppLayerTests; do
  cmake --build "$BUILD" --target "$t" -j 20 >/dev/null 2>&1
done
echo "--- CFDIoTests  (expect 331)"
"$BUILD/tests/unit/io/CFDIoTests" 2>&1 \
  | grep -E "tests from .* ran|PASSED|FAILED" | sed 's|^|    |'
echo "--- CFDAppLayerTests  (expect 32)"
"$BUILD/tests/unit/app/CFDAppLayerTests" 2>&1 \
  | grep -E "tests from .* ran|PASSED|FAILED" | sed 's|^|    |'
echo "--- CFDSpeciesTests excluding G8  (expect 343)"
"$BUILD/tests/unit/species/CFDSpeciesTests" --gtest_filter=-G8PremixedFlameTest.* 2>&1 \
  | grep -E "tests from .* ran|PASSED|FAILED" | sed 's|^|    |'
echo

echo "HOW TO READ THIS RUN"
echo "  C0 REPRODUCING and C1 FAILING is the REQUIRED result on the unchanged tree: that is the"
echo "  gate's fail-direction non-vacuity and its C8 negative control. If C1 PASSED here the gate"
echo "  would be vacuous and must not be frozen."
echo
echo "DONE $(date -Is)"

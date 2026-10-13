#!/bin/bash
# TD-037 -- VERIFICATION against the FROZEN gate (GATE.md bf4b9bcff8f70423...).
#
# Criteria run in the frozen order. The probe SOURCE is byte-identical to the frozen one; only the
# library it links changes, which is the two-library attribution CLAUDE.md §6 requires.
#
# C4 is checked MECHANICALLY: the window-32 trial record from this run is diffed against the
# baseline record in logs/01_dry_run.log. A human eyeballing fifteen rows of floats is not a check.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD037_drift_window_guard
DEFECTIVE_LIB=a9b01668be7ccb7f085f0dc65da0c7096d459c87797b0d650e05ba80974615cf
FROZEN_PROBE=d311d49f3eb49b27c98c79ae5a8a279adef60f53a3b2f5e4d06b90d440748122
BASE_LOG=$ITEM/logs/01_dry_run.log

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "nproc       $(nproc)"
echo

echo "=== STAGE A  rebuild, and prove the library CHANGED"
cmake --build "$BUILD" --target cfdcore -j 20
if [ $? -ne 0 ]; then
  echo "*** ABORT: the fixed library did not build."
  exit 1
fi
NEW_LIB=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo
echo "defective library (frozen)  $DEFECTIVE_LIB"
echo "fixed     library (now)     $NEW_LIB"
if [ "$NEW_LIB" = "$DEFECTIVE_LIB" ]; then
  echo "*** ABORT: THE LIBRARY DID NOT CHANGE. Either the fix did not land or the build is stale."
  echo "    Stale-binary results are not evidence (CLAUDE.md §8). Fails closed."
  exit 1
fi
echo "the two libraries differ -- this is a comparison between two real libraries"
echo
sha256sum "$REPO/src/species/PremixedFlame.cpp"
echo "  (pre-fix was f9f554bf9e1fe1fd29f6ce4c185e7d579fa5c5708958112bc83127e4a6717b6e)"
sha256sum "$REPO/include/cfd/species/PremixedFlame.hpp"
echo "  (MUST be unchanged: d140cb6cafc9dac6889614fb8dcbbf70aa8b83ea1d564370c6aacd447fea728e)"
sha256sum "$REPO/tests/unit/species/test_premixed_flame.cpp"
echo "  (MUST be unchanged: 8c2be7e82efba3ec1e1de4b6486a614c22d625997f5735a0cf33588d9eabb9fd)"
echo
P=$(sha256sum "$ITEM/tools/ladder_probe.cpp" | cut -d' ' -f1)
echo "probe source  $P"
if [ "$P" != "$FROZEN_PROBE" ]; then
  echo "*** ABORT: the probe source changed after the freeze. A moved instrument invalidates the"
  echo "    comparison. Fails closed."
  exit 1
fi
echo "matches the frozen hash"
echo

echo "=== STAGE B  C0 / C1 / C2 / C3 / C4 -- the ladder"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/ladder_probe.cpp" \
  -o "$ITEM/tools/td037_ladder_fixed" "$BUILD/src/libcfdcore.a"
if [ $? -ne 0 ]; then
  echo "*** ABORT: the probe did not compile."
  exit 1
fi
sha256sum "$ITEM/tools/td037_ladder_fixed"
echo
start=$(date +%s)
"$ITEM/tools/td037_ladder_fixed" | tee "$ITEM/logs/_verif_ladder.txt"
echo "ladder elapsed $(( $(date +%s) - start )) s   <- C7 reports, does not bound"
echo

echo "=== C4  MECHANICAL diff of the window-32 trial record against the frozen baseline"
extract() { sed -n '/driftWindow 32$/,/driftWindow 64$/p' "$1" | grep -E '^  +[0-9]+ +0\.'; }
extract "$BASE_LOG" > /tmp/td037_base32.txt
extract "$ITEM/logs/_verif_ladder.txt" > /tmp/td037_new32.txt
echo "baseline rows: $(wc -l < /tmp/td037_base32.txt)   post-fix rows: $(wc -l < /tmp/td037_new32.txt)"
if [ ! -s /tmp/td037_base32.txt ]; then
  echo "*** ABORT: the baseline record extracted EMPTY. C4 cannot be checked and must not be"
  echo "    reported as PASS. Fails closed."
  exit 1
fi
if diff -u /tmp/td037_base32.txt /tmp/td037_new32.txt > /tmp/td037_diff32.txt; then
  echo "C4  IDENTICAL -- the normal path is untouched at driftWindow 32   PASS"
else
  echo "C4  *** DIFFERS -- the fix reached the normal path   FAIL"
  cat /tmp/td037_diff32.txt
fi
echo

echo "=== STAGE C  C5 -- the test TD-032 broke. Must PASS now."
cmake --build "$BUILD" --target CFDSpeciesTests -j 20 >/dev/null 2>&1
start=$(date +%s)
"$BUILD/tests/unit/species/CFDSpeciesTests" \
  --gtest_filter=G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial 2>&1 \
  | grep -E "RUN|OK|FAILED|drift window:|drift-window spread|Failure|Which is" | sed 's|^|    |'
echo "    C5 elapsed $(( $(date +%s) - start )) s   against the 7200 s cap"
echo

echo "=== STAGE D  C6 -- regression outside the flame"
for t in CFDIoTests CFDAppLayerTests; do
  cmake --build "$BUILD" --target "$t" -j 20 >/dev/null 2>&1
done
NEWEST=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs stat -c '%Y' | sort -n | tail -1)
for p in tests/unit/io/CFDIoTests tests/unit/app/CFDAppLayerTests tests/unit/species/CFDSpeciesTests; do
  if [ "$(stat -c '%Y' "$BUILD/$p")" -lt "$NEWEST" ]; then
    echo "*** ABORT: $p is OLDER than the newest production source. Fails closed."
    exit 1
  fi
  echo "ok  $p  $(sha256sum "$BUILD/$p" | cut -c1-16)..."
done
echo
echo "--- CFDIoTests  (frozen expectation 331)"
"$BUILD/tests/unit/io/CFDIoTests" 2>&1 | grep -E "tests from .* ran|PASSED|FAILED" | sed 's|^|    |'
echo "--- CFDAppLayerTests  (frozen expectation 32)"
"$BUILD/tests/unit/app/CFDAppLayerTests" 2>&1 | grep -E "tests from .* ran|PASSED|FAILED" | sed 's|^|    |'
echo "--- CFDSpeciesTests excluding G8  (frozen expectation 343)"
"$BUILD/tests/unit/species/CFDSpeciesTests" --gtest_filter=-G8PremixedFlameTest.* 2>&1 \
  | grep -E "tests from .* ran|PASSED|FAILED" | sed 's|^|    |'
echo
echo "DONE $(date -Is)"

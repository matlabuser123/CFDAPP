#!/bin/bash
# TD-032 -- ATTRIBUTE the PASS -> FAIL on G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial.
#
# THE QUESTION. That test is recorded PASS twice on the pre-fix tree -- 744 s in
# TD004_premixed_flame/GATE.md:220 and logs/06, and 759 s in TD005_flame_speed_extraction/logs/08
# and summary.md:170. On the fixed tree it FAILS: `ASSERT_EQ(flame.status, Converged)` at
# test_premixed_flame.cpp:725 returns 2 = PremixedFlameStatus::NoFlame, at driftWindow 64, while
# windows 16 and 32 converge with S_L = 4.67194 identical.
#
# Two readings, and only a measurement separates them:
#
#   CAUSED      the fix made a previously-converging configuration stop finding a flame
#   UNMASKED    the configuration never had a flame, and the pre-fix ALTERNATION hid that. The
#               hot branch of the alternation was a nearly fully burnt field, which crosses
#               T_anchor everywhere, so `locateFront` succeeded on every hot-branch step and a
#               flame was always "found". The trial logs at windows 16 and 32 already show nearly
#               every trial reporting UNSETTLED, extinguished -- so extinction is real and
#               pre-existing.
#
# THE MEASUREMENT. Run THAT ONE TEST against both libraries. CLAUDE.md section 6 forbids blaming a
# change before building a baseline and measuring both, and the baseline here is a reconstruction
# whose sha256 must match the frozen pre-fix hash or the swap script refuses to write it.
#
# WHAT EACH OUTCOME MEANS, registered BEFORE the run so neither can be rationalised after:
#
#   pre-fix PASSES, post-fix FAILS   -> CAUSED. TD-032 introduced a capability regression. The
#                                       fix is still correct about the time level, but the item
#                                       is BLOCKED and the regression must be understood before
#                                       any closure.
#   pre-fix FAILS too                -> NOT CAUSED by the fix. Something else between TD-005's
#                                       recorded PASS and now did it -- TD-027's cap change to
#                                       5.0 is the obvious candidate -- and that is a separate
#                                       attribution, not TD-032's.
#   pre-fix PASSES and post-fix now  -> the earlier FAIL was not reproducible. Treat the whole
#   PASSES as well                      comparison as void and investigate the harness.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD032_lagged_time_level
SWAP=$ITEM/tools/swap_td032.py
TEST=G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial
BIN=$BUILD/tests/unit/species/CFDSpeciesTests

cd "$REPO" || exit 1

# The tree MUST end up fixed again, whatever happens below.
restore() {
  echo
  echo "=== RESTORING the fixed tree (runs even on failure)"
  python3 "$SWAP" in "$REPO" || echo "*** RESTORE FAILED -- the tree may be left pre-fix. CHECK IT."
  cmake --build "$BUILD" --target cfdcore CFDSpeciesTests -j 20 >/dev/null 2>&1
  echo "restored library $(sha256sum "$BUILD/src/libcfdcore.a" | cut -c1-16)..."
  echo "  (the verified fixed library is a9b01668be7ccb7f...)"
}
trap restore EXIT

echo "TD-032 ATTRIBUTION -- $TEST"
echo "date $(date -Is)"
echo

echo "=== STEP 1  reconstruct the PRE-FIX source and PROVE it against the frozen sha256"
python3 "$SWAP" out "$REPO"
if [ $? -ne 0 ]; then
  echo "*** ABORT: the reconstruction failed its hash proof. No measurement is possible."
  exit 1
fi
echo

echo "=== STEP 2  build the PRE-FIX library and test binary"
cmake --build "$BUILD" --target cfdcore CFDSpeciesTests -j 20 >/dev/null 2>&1
if [ $? -ne 0 ]; then
  echo "*** ABORT: the pre-fix tree did not build."
  exit 1
fi
PRE_LIB=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "pre-fix library  $PRE_LIB"
echo "  (the frozen lagged library was 3b94eb849f9c8277beded611db68c259b1b8d24e99f888af09a721f65da6e4b0)"
if [ "$PRE_LIB" = "3b94eb849f9c8277beded611db68c259b1b8d24e99f888af09a721f65da6e4b0" ]; then
  echo "  IDENTICAL to the frozen lagged library -- the baseline is the same one the dry run used."
else
  echo "  DIFFERENT from the frozen lagged library. Recorded, not hidden: the SOURCE hash is proved,"
  echo "  so the difference is in the link, not the code under test."
fi
echo

echo "=== STEP 3  run the ONE test on the PRE-FIX library"
start=$(date +%s)
"$BIN" --gtest_filter="$TEST" 2>&1 | grep -E "^\[|Failure|Expected|actual|Which is|drift window:|drift-window spread" | head -30
echo "pre-fix elapsed $(( $(date +%s) - start )) s"
echo

echo "=== STEP 4  restore the fix, rebuild, and re-run the SAME test for the pair"
python3 "$SWAP" in "$REPO" || { echo "*** ABORT: restore failed."; exit 1; }
cmake --build "$BUILD" --target cfdcore CFDSpeciesTests -j 20 >/dev/null 2>&1
POST_LIB=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "post-fix library $POST_LIB"
if [ "$PRE_LIB" = "$POST_LIB" ]; then
  echo "*** ABORT: the two libraries are IDENTICAL. The comparison would be vacuous."
  exit 1
fi
echo
start=$(date +%s)
"$BIN" --gtest_filter="$TEST" 2>&1 | grep -E "^\[|Failure|Expected|actual|Which is|drift window:|drift-window spread" | head -30
echo "post-fix elapsed $(( $(date +%s) - start )) s"
echo
echo "Read the pair against the pre-registered outcomes in this script's header."
echo "DONE $(date -Is)"

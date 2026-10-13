#!/bin/bash
# TD-037 -- FREEZE THE GATE. Run only after dry_run.md is complete and every criterion has been
# dry-run against the unchanged tree (CLAUDE.md §3, steps 5-8).
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD037_drift_window_guard

echo "=== TD-037 GATE FREEZE"
echo "date   $(date -Is)"
echo
echo "The gate is frozen at the hashes below. Any change to GATE.md after this point is an"
echo "AMENDMENT and needs its own file, its own dry-run and its own authorization (CLAUDE.md §5.3)."
echo

echo "--- SANITY: the baseline tree must be the one WITH TD-032's fix"
SRC=$(sha256sum "$REPO/src/species/PremixedFlame.cpp" | cut -d' ' -f1)
echo "PremixedFlame.cpp  $SRC"
if [ "$SRC" != "f9f554bf9e1fe1fd29f6ce4c185e7d579fa5c5708958112bc83127e4a6717b6e" ]; then
  echo "*** ABORT: this is NOT the post-TD-032 tree. TD-037's baseline must include that fix."
  exit 1
fi
echo "confirmed: post-TD-032"
echo

echo "--- THE GATE"
sha256sum "$ITEM/GATE.md"
sha256sum "$ITEM/dry_run.md"
sha256sum "$ITEM/audit.md"
echo

echo "--- PRODUCTION FILES IN SCOPE (the pre-fix state, which the fix will change)"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
sha256sum "$REPO/include/cfd/species/PremixedFlame.hpp"
echo

echo "--- TEST FILES (must be UNCHANGED by this item)"
sha256sum "$REPO/tests/unit/species/test_premixed_flame.cpp"
echo

echo "--- THE DEFECTIVE LIBRARY AND BINARIES THE DRY RUN MEASURED (C8's negative control)"
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$BUILD/tests/unit/io/CFDIoTests"
sha256sum "$BUILD/tests/unit/app/CFDAppLayerTests"
sha256sum "$BUILD/tests/unit/species/CFDSpeciesTests"
echo

echo "--- PROBE"
sha256sum "$ITEM/tools/ladder_probe.cpp"
sha256sum "$ITEM/tools/td037_ladder"
echo

echo "--- THE FROZEN CRITERIA, restated so this log stands alone"
cat <<'THRESHOLDS'
fixture  the test's makeSettings(): 500 cells, 0.010 m, anchorFraction 0.35, tolerance 1e-3,
         maxOuterIterations 40, driftSettleTolerance 0.05, maximumStepsPerTrial 20000.
         driftWindow in {16, 32, 64, 128} -- a ladder that STRADDLES the 40-step trial.

C0  REPRODUCE: on the unchanged tree, driftWindow 64 is not Converged and has an unmeasured trial
C1  status == Converged at driftWindow 16, 32, 64 AND 128
C2  |S_L - 4.671937| / 4.671937 <= 1.0e-3 at windows 16 and 32   (bound = the bisection tolerance)
C3  for every measured trial: tooLarge == (massFlux > final mdot), skipping trials within
    tolerance of the final mdot, where the side is genuinely undetermined
C4  at driftWindow 32 the 15-trial record is IDENTICAL to dry_run.md section 3
C5  G8PremixedFlameTest.TheAnswerIsInsensitiveToTheStepsPerTrial PASSES, spread below its own 0.02
C6  CFDIoTests 331 / CFDAppLayerTests 32 / CFDSpeciesTests 343 under -G8PremixedFlameTest.*
C7  no test breaches ctest --timeout 7200. Wall times REPORTED, NOT bounded -- see dry_run.md §5
C8  the defective library reproduces C0 and fails C1 and C5 -- SATISFIED by logs/01

STOP RULES: C0 passing on the baseline, or C2, C3 or C4 failing, each stop the item outright.
            trialSaysTooLarge() must keep failing closed; the unsided case must never guess.
            No commit, no push. TD-034 blocks commits regardless.
THRESHOLDS
echo
echo "FROZEN $(date -Is)"

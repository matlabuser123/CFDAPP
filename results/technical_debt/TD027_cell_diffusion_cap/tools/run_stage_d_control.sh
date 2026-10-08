#!/usr/bin/env bash
# TD-027 -- THE CAP-20 CONTROL FOR STAGE D. Is the no-flame control's 51 s PASS attributable to
# this fix, or was it always true and merely never measured?
#
# THE QUESTION. logs/06 measured WithReactionsDisabledNoFlamePropagates as PASS in 51 s at cap 5.
# On the unchanged tree it was only ever recorded as TIMEOUT at TD-004's **900 s HARNESS** cap, and
# TD-005's GATE.md section 5.2 listed it as "STILL NOT MEASURED AT THE REPOSITORY'S CAP". So the
# 51 s cannot be credited to TD-027 without measuring the same case at cap 20.
#
# WHY IT MATTERS MORE THAN TD-027's OWN VERDICT. If the control is exercisable, TD-005's C5 third
# control stops being UNEXERCISABLE and **G8's negative control 1 becomes available** -- which
# TD-020 recorded as a TD-006 blocker.
#
# THE RECONSTRUCTION IS PROVED, NOT ASSUMED. swap_td027.py reverses the edit and verifies the header
# against the sha256 in logs/00_freeze.log, refusing and restoring on a mismatch. So the control
# library is provably built from the tree the gate was frozen against.
#
# A DELIBERATE LIMITATION, STATED UP FRONT. This run caps the case at 1800 s, not the repository's
# 7200 s. Reason: the outer run window is 2 h, and a 7200 s case plus two whole-project rebuilds
# would exceed it -- and being killed mid-run could leave production reverted. 1800 s answers the
# ATTRIBUTION question decisively, because 51 s against >1800 s is a 35x separation. It does NOT
# answer "does this case pass at 7200 s on the unchanged tree?", which is TD-020's question and
# remains unmeasured. That distinction is the whole point of writing it down.
#
# THE TREE IS ALWAYS RESTORED: the trap re-applies the fix and rebuilds even if a step fails.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD="$REPO/build/release"
SPECIES="$BUILD/tests/unit/species/CFDSpeciesTests"
SWAP="/mnt/c/Users/Hasib/AppData/Local/Temp/claude/C--Users-Hasib-Desktop-CFDAPP/ee8e90df-238e-4c69-ad45-fe3ef00cba5d/scratchpad/swap_td027.py"
NAME=WithReactionsDisabledNoFlamePropagates
CONTROL_CAP=1800

cd "$REPO" || exit 1

restore() {
  echo
  echo "=== RESTORING the post-fix tree (runs even on failure)"
  python3 "$SWAP" in "$REPO"
  cmake --build "$BUILD" -j 20 >/dev/null 2>&1
  echo "rebuilt; CFDSpeciesTests $(sha256sum "$SPECIES" | cut -c1-16)..."
}
trap restore EXIT

echo "TD-027 -- STAGE D CAP-20 CONTROL"
echo "started $(date -Is)"
echo "control cap ${CONTROL_CAP}s (NOT the repository's 7200 s -- see the header note)"
echo "at cap 5 this case measured: PASS, 51 s (logs/06)"
echo

echo "=== STEP 1  reconstruct the pre-fix header and PROVE it against logs/00_freeze.log"
python3 "$SWAP" out "$REPO"
if [ $? -ne 0 ]; then
  echo "*** ABORT: reconstruction failed its hash proof."
  exit 1
fi
echo

echo "=== STEP 2  rebuild at cap 20, fail-closed"
cmake --build "$BUILD" -j 20
rc=$?
echo "build exit: $rc"
if [ "$rc" -ne 0 ]; then
  echo "*** ABORT: the cap-20 build failed."
  exit 1
fi
newest=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs ls -t 2>/dev/null | head -1)
if [ "$SPECIES" -nt "$newest" ]; then
  echo "test binary NEWER than newest source: OK"
else
  echo "*** ABORT: stale test binary."
  exit 1
fi
sha256sum "$SPECIES" "$REPO/include/cfd/species/PremixedFlame.hpp"
echo
echo "    cross-check: the cap in the built header"
grep -n 'maximumCellDiffusionNumber{' "$REPO/include/cfd/species/PremixedFlame.hpp"
echo

echo "=== STEP 3  run the case at cap 20"
start=$(date +%s)
out=$(timeout "$CONTROL_CAP" "$SPECIES" --gtest_filter="G8PremixedFlameTest.$NAME" 2>&1)
rc=$?
elapsed=$(( $(date +%s) - start ))
echo "$out" | grep -E 'reactions off|OK \]|FAILED|Failure' | sed 's/^/    /' | head -8
echo
if [ "$rc" -eq 124 ]; then
  echo "-> TIMEOUT after ${elapsed}s at the ${CONTROL_CAP}s CONTROL cap"
  echo "   51 s at cap 5 against >${CONTROL_CAP}s at cap 20 is a ${CONTROL_CAP}x-order separation:"
  echo "   THE PASS IS ATTRIBUTABLE TO TD-027."
  echo "   NOT established: whether this case passes at the repository's 7200 s on the unchanged"
  echo "   tree. That remains TD-020's open question."
elif [ "$rc" -eq 0 ]; then
  echo "-> PASS (${elapsed}s) at cap 20"
  echo "   So the case was ALWAYS exercisable and the 900 s TIMEOUT in TD-004's baseline was a"
  echo "   HARNESS artefact, like the other two that item recorded. TD-027 did NOT fix this, and"
  echo "   the 51 s is not its achievement -- but G8's negative control 1 is available either way,"
  echo "   which is the consequence that matters."
else
  echo "-> FAIL (${elapsed}s, rc=$rc) at cap 20 -- a verdict, not a timeout. Report as such."
fi
echo "finished $(date -Is)"

#!/usr/bin/env bash
# TD-005 -- C5's THIRD CONTROL, measured at the repository's cap instead of assumed.
#
# The register requires the extractor to FAIL with reactions disabled. That control is
# `WithReactionsDisabledNoFlamePropagates`, recorded as "TIMEOUT after 900s" in TD-004's baseline --
# but at TD-004's HARNESS cap, not this repository's 7200 s (see run_tests_at_repo_cap.sh for the
# evidence). GATE.md C5 may not declare the control "UNEXERCISABLE" on the strength of a cap that
# is not the repository's, and it may not declare it WORKING either. So it is measured.
#
# WHY IT GETS ITS OWN RUN. TD-020's register entry describes why this case is expensive, and the
# cost is structural rather than incidental:
#
#   "With reactions off the seeded front washes out slowly rather than vanishing, so early trials
#    still locate a front and return a sided verdict; the bracket then grinds through
#    maxOuterIterations = 2000 at up to 20 000 steps each. The !sided -> NoFlame short-circuit
#    fires only once the front is gone, so the no-flame case costs MORE than the flame case."
#
# So this may legitimately consume the full 7200 s, which is why it is not run alongside the
# plateau measurement -- CLAUDE.md's hardware policy gives an authoritative run the machine.
#
# FORBIDDEN, from TD-020's entry, and restated here because this script is where the temptation
# would land: raising the test's timeout, or lowering maxOuterIterations in the test. Both make the
# control finish without making it work.
#
# EITHER OUTCOME IS A RESULT:
#   a VERDICT         -> the control is exercisable today; C5 can require it, and TD-020 is a
#                        performance item rather than a correctness blocker for this gate.
#   TIMEOUT at 7200 s -> the control is genuinely unexercisable, C5 must DECLARE it so, and G8
#                        cannot close with both of its negative controls exercised (TD-006).
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD="$REPO/build/release"
BIN="$BUILD/tests/unit/species/CFDSpeciesTests"
CAP=7200
NAME=WithReactionsDisabledNoFlamePropagates

cd "$REPO" || exit 1

echo "TD-005 -- C5 control 3 at the repository cap"
echo "per-test timeout ${CAP}s   (ci.yml: ctest --timeout 7200)"
echo "started $(date -Is)"
echo

echo "=== REBUILD, fail-closed"
cmake --build "$BUILD" --target CFDSpeciesTests -j 20
rc=$?
echo "build exit: $rc"
if [ "$rc" -ne 0 ]; then
  echo "*** ABORT: the test binary did not build."
  exit 1
fi
newest=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs ls -t | head -1)
if [ "$BIN" -nt "$newest" ]; then
  echo "binary newer than newest source ($newest): OK"
else
  echo "*** ABORT: stale test binary against $newest"
  exit 1
fi
sha256sum "$BIN"
echo

echo "=== $NAME"
start=$(date +%s)
timeout "$CAP" "$BIN" --gtest_filter="G8PremixedFlameTest.$NAME" 2>&1 | sed 's/^/    /'
rc=${PIPESTATUS[0]}
elapsed=$(( $(date +%s) - start ))
echo
if [ "$rc" -eq 124 ]; then
  echo "-> TIMEOUT after ${elapsed}s AT THE REPOSITORY CAP. The control is genuinely"
  echo "   UNEXERCISABLE and C5 must declare it, not claim it. This is TD-020."
elif [ "$rc" -eq 0 ]; then
  echo "-> PASS (${elapsed}s). The control WORKS at the repository cap; the 900 s figure was a"
  echo "   harness artefact, and TD-020 is a cost item rather than a blocker for C5."
else
  echo "-> FAIL (${elapsed}s, rc=$rc). A verdict: the control runs but does not hold. That is a"
  echo "   REAL defect in the control's subject and must be reported, not worked around."
fi
echo "finished $(date -Is)"
exit 0

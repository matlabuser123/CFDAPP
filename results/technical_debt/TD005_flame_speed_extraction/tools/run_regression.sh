#!/usr/bin/env bash
# TD-005 -- REGRESSION for the C4d fix, at the REPOSITORY's cap, reported TEST BY TEST.
#
# GATE.md section 5.1 requires G8PremixedFlameTest be reported per test rather than as a total,
# "because a count would hide one test improving while another degrades". That is exactly the
# situation here: C4d turns a vacuous assertion into a real one, so a test is EXPECTED to start
# failing, and a total would either hide it or look like damage.
#
# THE EXPECTED MOVEMENT, stated before the run so it is a prediction and not a rationalisation:
#
#   TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength
#       BEFORE  PASS 997 s (logs/03). It passed partly VACUOUSLY: its front clause compared
#               `anchorFraction * domainLength` against itself.
#       AFTER   EXPECTED TO FAIL. The clause is now real, and at anchor 0.30 the front sits in
#               the bad mode. logs/02 measured the bad mode's front 46.6 cells off-anchor against
#               the clause's 10-cell tolerance.
#       READING A vacuous instrument starting to detect a defect C3 already records as a frozen
#               FAIL. It is NOT a regression caused by the fix, and the distinction is the whole
#               reason this run is per-test.
#
#   every other test   EXPECTED UNCHANGED. The two-library control (logs/07) proved every computed
#                      quantity bitwise identical across the fix, so no test that reads a computed
#                      quantity can move. Only `flamePosition` changed, and it has exactly one
#                      reader in the tree -- the clause above.
#
# NOT RUN HERE: WithReactionsDisabledNoFlamePropagates. It is C5's unmeasured control and TD-020's
# cost problem, and it may consume the full 7200 s on its own, so it gets a separate run rather
# than pushing this one past the session's budget.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD="$REPO/build/release"
BIN="$BUILD/tests/unit/species/CFDSpeciesTests"
CAP=7200

cd "$REPO" || exit 1

echo "TD-005 -- REGRESSION, post-C4d-fix"
echo "per-test timeout ${CAP}s   (ci.yml: ctest --timeout 7200; gtest_discover_tests => per case)"
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
newest=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs ls -t 2>/dev/null | head -1)
if [ "$BIN" -nt "$newest" ]; then
  echo "binary NEWER than newest source ($newest): OK"
else
  echo "*** ABORT: stale test binary."
  exit 1
fi
sha256sum "$BIN" "$REPO/src/species/PremixedFlame.cpp" \
          "$REPO/include/cfd/species/PremixedFlame.hpp" \
          "$REPO/tests/unit/species/test_premixed_flame.cpp"
echo

echo "=== C7  CFDSpeciesTests excluding G8PremixedFlameTest   (expect 343, unchanged)"
"$BIN" --gtest_filter=-G8PremixedFlameTest.* 2>&1 | tail -4 | sed 's/^/    /'
echo

echo "=== G8PremixedFlameTest, TEST BY TEST"
for name in \
  MalformedConfigurationsAreRefused \
  TheBurntCompositionConsumesTheFuelExactlyAndConservesMass \
  TheObviousChecksAreVacuousOnTheSeededProfile \
  ItConvergesAndReachesTheIndependentlyDerivedAdiabaticTemperature \
  TheConsumptionIntegralAgreesWithTheEigenvalue \
  TheAnswerIsInsensitiveToTheStepsPerTrial \
  TheEigenvalueIsIndependentOfTheInitialMassFluxGuess \
  TenTimesThePreExponentialMovesTheFlameSpeedFar \
  AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue \
  TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength \
  TheFlameSpeedConvergesUnderMeshRefinement ; do
  start=$(date +%s)
  out=$(timeout "$CAP" "$BIN" --gtest_filter="G8PremixedFlameTest.$name" 2>&1)
  rc=$?
  elapsed=$(( $(date +%s) - start ))
  if [ "$rc" -eq 124 ]; then
    verdict="TIMEOUT at ${CAP}s"
  elif [ "$rc" -eq 0 ]; then
    verdict="PASS"
  else
    verdict="FAIL (rc=$rc)"
  fi
  printf '%-66s %-18s %6ss\n' "$name" "$verdict" "$elapsed"
  # the failing assertions, so a FAIL is attributable rather than just counted
  echo "$out" | grep -E 'Failure|Expected:|Actual:|Which is:|error:' | sed 's/^/      /' | head -8
done

echo
echo "NOT RUN: WithReactionsDisabledNoFlamePropagates -- C5's unmeasured control, TD-020's cost"
echo "problem, run separately by run_noflame_control_at_repo_cap.sh."
echo "finished $(date -Is)"

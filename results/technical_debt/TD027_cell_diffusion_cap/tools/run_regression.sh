#!/usr/bin/env bash
# TD-027 -- REGRESSION: C5 (counts outside the flame) and C6 (the per-test cap).
#
#   usage:  run_regression.sh <stage>
#     stage A   rebuild (fail-closed) + C5 + the four instant G8 cases + the three ~1000 s cases
#     stage B   the two ~2000-3000 s cases
#     stage C   the two ~3200-3640 s cases
#     stage D   WithReactionsDisabledNoFlamePropagates alone, at the full 7200 s cap
#
# WHY STAGED. cap = 5 quadruples the step count, so C6 is ~4.1 h of solver time -- past one run
# window. The split is by PREDICTED cost (GATE.md section 4.5), not by convenience, and every stage
# runs each case at the repository's own 7200 s per-case cap so no stage can soften C6.
#
# C6 IS A CRITERION, NOT A FOOTNOTE: a timeout is a FAILURE of this gate. GATE.md section 4.5 names
# the forbidden repairs -- raising the cap, cutting maxOuterIterations, coarsening the fixture, or
# splitting a test so it measures less.
set -u

STAGE="${1:-}"
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD="$REPO/build/release"
SPECIES="$BUILD/tests/unit/species/CFDSpeciesTests"
CAP=7200
cd "$REPO" || exit 1

echo "TD-027 REGRESSION -- stage ${STAGE}"
echo "GATE.md sha256 56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75"
echo "per-case timeout ${CAP}s (ci.yml:147). maximumCellDiffusionNumber = 5.0"
echo "started $(date -Is)"
echo

if [ "$STAGE" = "A" ]; then
  echo "=== REBUILD, fail-closed (the header changed, so every dependent target must recompile)"
  cmake --build "$BUILD" -j 20
  rc=$?
  echo "build exit: $rc"
  if [ "$rc" -ne 0 ]; then
    echo "*** ABORT: the build failed. No stale binary is run."
    exit 1
  fi
  newest=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs ls -t 2>/dev/null | head -1)
  if [ "$SPECIES" -nt "$newest" ]; then
    echo "test binary NEWER than newest source ($newest): OK"
  else
    echo "*** ABORT: stale test binary."
    exit 1
  fi
  sha256sum "$SPECIES" "$REPO/include/cfd/species/PremixedFlame.hpp"
  echo
  echo "=== C5  the counts outside the flame"
  for t in CFDSpeciesTests:-G8PremixedFlameTest.*:343 CFDIoTests::331 CFDAppLayerTests::32; do
    name="${t%%:*}"; rest="${t#*:}"; filt="${rest%%:*}"; want="${rest##*:}"
    bin=$(find "$BUILD/tests" -name "$name" -type f | head -1)
    if [ -z "$bin" ]; then echo "  $name: BINARY NOT FOUND"; continue; fi
    if [ -n "$filt" ]; then out=$("$bin" --gtest_filter="$filt" 2>&1); else out=$("$bin" 2>&1); fi
    got=$(echo "$out" | grep -oE '\[  PASSED  \] [0-9]+ test' | grep -oE '[0-9]+' | head -1)
    fail=$(echo "$out" | grep -cE '^\[  FAILED  \]' || true)
    printf '  %-20s passed %-5s expected %-5s failed-lines %-3s %s\n' "$name" "${got:-?}" "$want" \
           "$fail" "$([ "${got:-x}" = "$want" ] && echo OK || echo '*** MISMATCH ***')"
  done
  echo
  TESTS="MalformedConfigurationsAreRefused
TheBurntCompositionConsumesTheFuelExactlyAndConservesMass
TheObviousChecksAreVacuousOnTheSeededProfile
AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue
ItConvergesAndReachesTheIndependentlyDerivedAdiabaticTemperature
TheConsumptionIntegralAgreesWithTheEigenvalue
TheFlameSpeedConvergesUnderMeshRefinement"
elif [ "$STAGE" = "B" ]; then
  TESTS="TheEigenvalueIsIndependentOfTheInitialMassFluxGuess
TheAnswerIsInsensitiveToTheStepsPerTrial"
elif [ "$STAGE" = "C" ]; then
  TESTS="TenTimesThePreExponentialMovesTheFlameSpeedFar
TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength"
elif [ "$STAGE" = "D" ]; then
  TESTS="WithReactionsDisabledNoFlamePropagates"
  echo "NOTE: this case was ALREADY unmeasured at 7200 s before the change (TD-020) and scales"
  echo "with 1/dt like everything else. GATE.md section 5.2 names it a permitted non-runner: a"
  echo "timeout here is TD-020's cost problem made worse, NOT a new failure of this gate. It is"
  echo "reported either way."
  echo
else
  echo "usage: run_regression.sh A|B|C|D"
  exit 2
fi

echo "=== C6  G8PremixedFlameTest, per case, at the repository cap"
for name in $TESTS; do
  start=$(date +%s)
  out=$(timeout "$CAP" "$SPECIES" --gtest_filter="G8PremixedFlameTest.$name" 2>&1)
  rc=$?
  elapsed=$(( $(date +%s) - start ))
  if [ "$rc" -eq 124 ]; then verdict="TIMEOUT at ${CAP}s"
  elif [ "$rc" -eq 0 ]; then verdict="PASS"
  else verdict="FAIL (rc=$rc)"; fi
  printf '%-66s %-18s %6ss\n' "$name" "$verdict" "$elapsed"
  echo "$out" | grep -E 'Failure|Expected|Actual|difference|S_L =|spread|plateau' | sed 's/^/      /' | head -6
done

echo
echo "finished $(date -Is)"

#!/usr/bin/env bash
# P18.1 section 29 -- non-vacuity controls.
#
# Each control deliberately removes ONE invariant and requires the tests that
# guard it to FAIL. A suite that still passes when sum(Y_i)=1 enforcement is
# deleted is not qualified -- section 29 says so outright.
#
# Methodology is P15's, reused unchanged:
#   * every mutation is restored from a pre-control copy, and the restore is
#     VERIFIED BY sha256 rather than assumed -- a control that leaves
#     production source modified is worse than no control at all;
#   * the test binary is DELETED before each rebuild and required back, so a
#     mutation that fails to compile can never leave a stale binary that then
#     "passes" and is recorded UNDETECTED;
#   * each control asserts the MECHANISM -- the named tests that must fail --
#     not merely a nonzero exit, because a failure for an unrelated reason is
#     a false DETECTED.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1

BUILD=build/final
BIN=$BUILD/tests/unit/species/CFDSpeciesTests
FILES="src/species/MassFractionState.cpp src/species/SpeciesRegistry.cpp"
BK=/tmp/p18_1_controls
rm -rf "$BK"; mkdir -p "$BK"

for f in $FILES; do cp "$f" "$BK/$(echo "$f" | tr / _)"; done
sha256sum $FILES > "$BK/pre.sha256"
echo "=== pre-control hashes ==="
sed 's/^/  /' "$BK/pre.sha256"
echo ""

restore() {
  for f in $FILES; do cp "$BK/$(echo "$f" | tr / _)" "$f"; done
  if sha256sum -c "$BK/pre.sha256" --quiet; then
    echo "  RESTORED (sha256 verified)"
  else
    echo "  *** RESTORE FAILED -- stopping rather than continuing on a mutated tree ***"
    exit 9
  fi
}

rebuild() {
  rm -f "$BIN"
  cmake --build "$BUILD" --target CFDSpeciesTests -j 20 > /tmp/p18_1_ctl_build.log 2>&1
  if [ ! -x "$BIN" ]; then
    echo "  BUILD FAILED under this control -- the control is INVALID, not a detection"
    grep -E 'error:' /tmp/p18_1_ctl_build.log | head -5 | sed 's/^/    /'
    return 1
  fi
  return 0
}

# run_control <label> <expected-failing-test-names...>
# DETECTED only if every named test actually fails.
run_control() {
  local label="$1"; shift
  local expected=("$@")
  if ! rebuild; then
    echo "  $label: CONTROL INVALID (build)"
    return 1
  fi
  "$BIN" --gtest_filter='SpeciesRegistryTest.*:MassFractionStateTest.*' \
    > /tmp/p18_1_ctl_run.log 2>&1
  local failed
  failed=$(grep -c '^\[  FAILED  \].*\.' /tmp/p18_1_ctl_run.log)
  echo "  tests failing under this control: $((failed / 2))"

  local all_detected=1
  for t in "${expected[@]}"; do
    if grep -q "^\[  FAILED  \] $t" /tmp/p18_1_ctl_run.log; then
      echo "    DETECTED by  $t"
    else
      echo "    *** NOT DETECTED by $t -- that test is vacuous for this invariant ***"
      all_detected=0
    fi
  done
  if [ "$all_detected" -eq 1 ]; then
    echo "  $label: DETECTED"
  else
    echo "  $label: **VACUOUS**"
  fi
  return 0
}

echo "=================================================================="
echo "C1 -- BOUNDS CHECK REMOVED (0 <= Y_i <= 1 widened to +/-1e9)"
echo "=================================================================="
sed -i 's|if (value < 0.0 \|\| value > 1.0) {|if (value < -1.0e9 \|\| value > 1.0e9) {|' \
  src/species/MassFractionState.cpp
grep -q 'value < -1.0e9' src/species/MassFractionState.cpp || { echo "  MUTATION DID NOT APPLY"; exit 8; }
run_control "C1 bounds" \
  MassFractionStateTest.RejectsNegativeMassFraction \
  MassFractionStateTest.RejectsATinyNegativeMassFraction \
  MassFractionStateTest.RejectsMassFractionAboveOne \
  MassFractionStateTest.BoundsDiagnosticNamesSpeciesCellAndValue
restore
echo ""

echo "=================================================================="
echo "C2 -- SUM-TO-ONE CHECK REMOVED (tolerance widened to 1e9)"
echo "=================================================================="
sed -i 's|if (std::abs(sum - 1.0) > tolerance) {|if (std::abs(sum - 1.0) > 1.0e9) {|' \
  src/species/MassFractionState.cpp
grep -q '1.0) > 1.0e9' src/species/MassFractionState.cpp || { echo "  MUTATION DID NOT APPLY"; exit 8; }
run_control "C2 sum" \
  MassFractionStateTest.RejectsASumMeaningfullyAboveOne \
  MassFractionStateTest.RejectsASumMeaningfullyBelowOne \
  MassFractionStateTest.SumCriterionIsNonVacuousJustInsideAndJustOutside \
  MassFractionStateTest.ConstructionNeverNormalisesSilently \
  MassFractionStateTest.DetectsExactlyOneInvalidCellAmongManyValidOnes
restore
echo ""

echo "=================================================================="
echo "C3 -- FINITENESS CHECK REMOVED"
echo "=================================================================="
sed -i 's|if (!std::isfinite(value)) {|if (std::isfinite(value) \&\& !std::isfinite(value)) {|' \
  src/species/MassFractionState.cpp
grep -q 'std::isfinite(value) && !std::isfinite(value)' src/species/MassFractionState.cpp \
  || { echo "  MUTATION DID NOT APPLY"; exit 8; }
# NOTE, recorded before running: only NaN is expected to escape. +Inf is still
# caught by `value > 1` and -Inf by `value < 0`, so RejectsInf must STILL PASS.
# A control that claimed RejectsInf would fail here would be wrong about its
# own mechanism.
run_control "C3 finiteness" MassFractionStateTest.RejectsNaN
echo "  (expected: RejectsInf still passes -- Inf is caught by the bounds check)"
grep -q '^\[       OK \] MassFractionStateTest.RejectsInf' /tmp/p18_1_ctl_run.log \
  && echo "    CONFIRMED: RejectsInf still passes" \
  || echo "    NOTE: RejectsInf did not pass -- mechanism differs from the prediction"
restore
echo ""

echo "=================================================================="
echo "C4 -- SPECIES IDENTITY MISMATCH (registry equality ignores names/order)"
echo "=================================================================="
sed -i 's|return lhs.names() == rhs.names();|return lhs.names().size() == rhs.names().size();|' \
  src/species/SpeciesRegistry.cpp
grep -q 'names().size() == rhs.names().size()' src/species/SpeciesRegistry.cpp \
  || { echo "  MUTATION DID NOT APPLY"; exit 8; }
# NOTE: these live in the SpeciesRegistryTest suite, not MassFractionStateTest.
# The first run of this harness named them under the wrong suite and therefore
# reported C4 VACUOUS while the control had in fact worked -- preserved at
# logs/04_negative_controls.log and diagnosed at logs/05_c4_investigation.log.
# The harness failed CLOSED (claiming vacuity rather than a false detection),
# which is the right direction for an instrument to fail.
run_control "C4 identity" \
  SpeciesRegistryTest.LookupIsIndependentOfDeclarationOrder \
  SpeciesRegistryTest.EqualityDistinguishesDifferentSpecies
restore
echo ""

echo "=================================================================="
echo "C5 -- THRESHOLD WEAKENED (frozen 4*eps*N replaced by 1e-3)"
echo "=================================================================="
sed -i 's|return 4.0 \* std::numeric_limits<Real>::epsilon() \* static_cast<Real>(speciesCount);|return 1.0e-3;|' \
  src/species/MassFractionState.cpp
grep -q 'return 1.0e-3;' src/species/MassFractionState.cpp || { echo "  MUTATION DID NOT APPLY"; exit 8; }
run_control "C5 weakened threshold" \
  MassFractionStateTest.ToleranceIsTheFrozenFourEpsN \
  MassFractionStateTest.SumCriterionIsNonVacuousJustInsideAndJustOutside
restore
echo ""

echo "=================================================================="
echo "FINAL TREE STATE"
echo "=================================================================="
sha256sum -c "$BK/pre.sha256" && echo "  every control restored; production source is byte-identical to pre-control"
echo ""
echo "=== rebuilding clean and re-running to prove the suite is green again ==="
rebuild || exit 1
"$BIN" --gtest_filter='SpeciesRegistryTest.*:MassFractionStateTest.*' 2>&1 | tail -4

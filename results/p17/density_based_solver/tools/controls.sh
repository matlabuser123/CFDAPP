#!/usr/bin/env bash
# P17.8 -- the Group N non-vacuity controls, N1..N7, exactly as the frozen gate lists them.
#
# Each plants ONE defect in production source, rebuilds, runs the criteria the gate says it MUST
# BREAK and the ones it MUST STILL PASS, then restores and verifies the restore by SHA-256.
#
# TWO GUARDS AGAINST A VACUOUS CONTROL, both of which have caught a fail-open probe in this project
# before:
#   1. after planting, the file's SHA-256 must CHANGE. A sed that matched nothing leaves the file
#      untouched and every test then "passes", which reads exactly like a control that failed to
#      detect anything.
#   2. after restoring, the SHA-256 must return to the pristine value, or the run is aborted so a
#      planted defect cannot leak into the regression.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/density_based_solver
SRC=src/compressible/DensityBasedSolver.cpp
WORK=$P17/controls
mkdir -p "$WORK"

UNIT=build/qual-final/tests/unit/compressible/CFDCompressibleTests
CANON=build/qual-final/tests/integration/compressible/CFDDensityBasedCanonicalTests

cp "$SRC" "$WORK/DensityBasedSolver.cpp.pristine"
PRISTINE=$(sha256sum "$SRC" | awk '{print $1}')
echo "P17.8 NON-VACUITY CONTROLS"
echo "date: $(date -u +%FT%TZ)"
echo "pristine $SRC sha256: $PRISTINE"
echo ""

restore() {
  cp "$WORK/DensityBasedSolver.cpp.pristine" "$SRC"
  local now
  now=$(sha256sum "$SRC" | awk '{print $1}')
  if [ "$now" != "$PRISTINE" ]; then
    echo "*** RESTORE FAILED -- sha $now != $PRISTINE. ABORTING so no plant leaks into the tree."
    exit 3
  fi
}

# run_one <binary> <gtest filter>  -> prints PASS or FAIL
run_one() {
  if OMP_NUM_THREADS=1 "$1" --gtest_filter="$2" > "$WORK/last_run.log" 2>&1; then
    echo "PASS"
  else
    echo "FAIL"
  fi
}

declare -A BIN
BIN[R1a]=$UNIT;  BIN[R1b]=$UNIT;  BIN[G1]=$UNIT;  BIN[WALL]=$UNIT
BIN[C1]=$UNIT;   BIN[Q1]=$UNIT;   BIN[K1]=$UNIT;  BIN[K4]=$UNIT
BIN[K3]=$UNIT;   BIN[U1]=$UNIT;   BIN[U2]=$UNIT;  BIN[P4]=$UNIT
BIN[M3]=$UNIT;   BIN[G2]=$CANON;  BIN[C4]=$CANON; BIN[G1g]=$UNIT; BIN[G1m]=$CANON

declare -A FILTER
FILTER[R1a]='DensityBasedResidualTest.R1_OwnerAndNeighbourContributionsCancelExactly'
FILTER[R1b]='DensityBasedResidualTest.R1_ResidualIsExactlyPlusFOwnerMinusFNeighbourBitwise'
FILTER[G1]='DensityBasedConservationTest.G1_ClosedDomainConservesMassMomentumAndEnergy'
FILTER[G1g]='DensityBasedConservationTest.G1_ClosedDomainConservesOnAGradedMeshToo'
FILTER[G1m]='DensityBasedCanonicalIntegration.G1_ClosedDomainMomentumMatchesTheIntegratedWallForce'
FILTER[WALL]='DensityBasedConservationTest.InviscidWallCarriesNoMassOrEnergy'
FILTER[C1]='DensityBasedCanonicalTest.C1_UniformGridAlignedFlowIsPreserved'
FILTER[Q1]='DensityBasedStateTest.Q1_PrimitiveConservedRoundTripIsExact'
FILTER[K1]='DensityBasedCflTest.K1_TimeStepIsExactlyLinearInCfl'
FILTER[K4]='DensityBasedCflTest.K4_AgreesWithTheExistingCalculateCflInTheZeroSoundSpeedLimit'
FILTER[K3]='DensityBasedCflTest.K6_EdgeCasesGiveFinitePositiveTimeSteps'
FILTER[U1]='DensityBasedUpdateTest.U1_UpdateIsExactlyQMinusDtOverVTimesR'
FILTER[U2]='DensityBasedUpdateTest.U2_GlobalModeUsesOneDtForEveryCell'
FILTER[P4]='DensityBasedRobustnessTest.P4_NothingIsEverClamped'
FILTER[M3]='DensityBasedConvergenceTest.M3_TheResidualReferenceScaleIsCapturedOnceAtIterationZero'
FILTER[G2]='DensityBasedCanonicalIntegration.G2_OpenDomainBalancesAgainstTheIntegratedBoundaryFlux'
FILTER[C4]='DensityBasedCanonicalIntegration.C4_SmoothAdvectedPulseConvergesToTheAnalyticSolution'

overall=0

# control <id> <description> <must-break> <must-pass> [report-only]
#
# REPORT-ONLY is for criteria the FROZEN GATE names whose outcome is determined by algebra rather
# than by the implementation -- a criterion that provably cannot see the planted defect, or that
# provably must also break. Their results are printed in full and do NOT count as failures, and
# every one of them is justified in writing in AMENDMENT_N2_N3_control_predictions.md. The frozen
# acceptance_gate.md is NOT edited: this is the amendment route its section 15 prescribes, and the
# original prediction stays on the record exactly as written.
control() {
  local id="$1" desc="$2" breaks="$3" passes="$4" reportOnly="${5:-}"
  local now
  now=$(sha256sum "$SRC" | awk '{print $1}')
  echo "--- $id: $desc"
  if [ "$now" = "$PRISTINE" ]; then
    echo "    *** THE PLANT DID NOT APPLY -- the file is unchanged, so every result below would be"
    echo "    *** vacuous. This is the fail-open shape the header warns about."
    overall=$((overall+1))
    restore
    return
  fi
  echo "    planted sha256: $now"
  if ! cmake --build build/qual-final --target CFDCompressibleTests CFDDensityBasedCanonicalTests -j 16 \
       > "$WORK/build_$id.log" 2>&1; then
    echo "    plant does not COMPILE -- treated as a failed control, not a detection"
    grep -aE '(^|[^a-zA-Z])error:' "$WORK/build_$id.log" | head -4 | sed 's/^/      /'
    overall=$((overall+1))
    restore
    return
  fi
  local bad=0
  for c in $breaks; do
    local r; r=$(run_one "${BIN[$c]}" "${FILTER[$c]}")
    if [ "$r" = "FAIL" ]; then
      printf '    MUST BREAK  %-5s %s   detected\n' "$c" "$r"
    else
      printf '    MUST BREAK  %-5s %s   *** NOT DETECTED ***\n' "$c" "$r"
      bad=$((bad+1))
    fi
  done
  for c in $passes; do
    local r; r=$(run_one "${BIN[$c]}" "${FILTER[$c]}")
    if [ "$r" = "PASS" ]; then
      printf '    must pass   %-5s %s\n' "$c" "$r"
    else
      printf '    must pass   %-5s %s   *** COLLATERAL -- the control is less targeted than the gate predicted ***\n' "$c" "$r"
      bad=$((bad+1))
    fi
  done
  for c in $reportOnly; do
    local r; r=$(run_one "${BIN[$c]}" "${FILTER[$c]}")
    printf '    REPORT ONLY %-5s %s   (gate prediction settled by algebra -- see the amendment)\n' "$c" "$r"
  done
  [ "$bad" -eq 0 ] || overall=$((overall+1))
  restore
  echo "    restored, sha256 verified"
  echo ""
}

# ---------------------------------------------------------------------------------------------
# N1 -- the neighbour's -F contribution dropped
# ---------------------------------------------------------------------------------------------
sed -i 's|^      r\.density\[neighbour\] -= f\.mass;|      // N1 dropped|;
        s|^      r\.momentum\[neighbour\] -= f\.momentum;|      // N1 dropped|;
        s|^      r\.totalEnergy\[neighbour\] -= f\.energy;|      // N1 dropped|' "$SRC"
control N1 "the neighbour's -F contribution dropped" "R1b G1 G1m G2 C1" "Q1 K1"

# ---------------------------------------------------------------------------------------------
# N2 -- the update uses dt instead of dt/V
# ---------------------------------------------------------------------------------------------
sed -i 's|const Real scale = dt\[i\] / mesh\.cell(i)\.volume();|const Real scale = dt[i];  // N2|' "$SRC"
control N2 "the update uses dt instead of dt/V" "U1 G1g" "R1b" "C1 G1"

# ---------------------------------------------------------------------------------------------
# N3 -- the acoustic term +a_f dropped from the CFL
# ---------------------------------------------------------------------------------------------
sed -i 's|speedSum += (std::abs(un) + faceValue\.speedOfSound) \* face\.area();|speedSum += (std::abs(un)) * face.area();  // N3|' "$SRC"
control N3 "the acoustic term +a_f dropped from the CFL" "K4" "K1" "K3"

# ---------------------------------------------------------------------------------------------
# N4 -- density CLAMPED to a floor instead of retrying
#
# The floor is 0.95 kg/m^3, sized against the cases rather than picked at random: G1's closed box
# spans [0.7, 1.3] and G2's open domain [0.935, 1.265], so the clamp BITES in both; C1's uniform
# state is 1.2, so it does not fire there -- which is exactly the "must still pass C1" the gate
# predicts, and it is a property of the number, not a hope.
# ---------------------------------------------------------------------------------------------
sed -i 's|^    out\.density\[i\] = q\.density\[i\] - scale \* r\.density\[i\];|    out.density[i] = std::max(q.density[i] - scale * r.density[i], 0.95);  // N4|;
        s|^    c\.density = q\.density\[i\];|    c.density = std::max(q.density[i], 0.95);  // N4|' "$SRC"
control N4 "density clamped to a floor instead of retrying" "G1 G2 P4" "C1"

# ---------------------------------------------------------------------------------------------
# N5 -- the residual reference scale re-captured every iteration
# ---------------------------------------------------------------------------------------------
sed -i 's|^    if (!referenceCaptured) {|    if (true) {  // N5 re-capture every iteration|' "$SRC"
control N5 "the residual reference re-captured every iteration" "M3" "K1"

# ---------------------------------------------------------------------------------------------
# N6 -- the wall exterior state does NOT mirror the normal velocity
# ---------------------------------------------------------------------------------------------
sed -i 's|interior\.velocity\.x - 2\.0 \* un \* outwardUnitNormal\.x|interior.velocity.x - 0.0 * un * outwardUnitNormal.x|;
        s|interior\.velocity\.y - 2\.0 \* un \* outwardUnitNormal\.y|interior.velocity.y - 0.0 * un * outwardUnitNormal.y|;
        s|interior\.velocity\.z - 2\.0 \* un \* outwardUnitNormal\.z|interior.velocity.z - 0.0 * un * outwardUnitNormal.z|' "$SRC"
control N6 "the wall exterior state does not mirror the normal velocity" "WALL G1m" "C1"

# ---------------------------------------------------------------------------------------------
# N7 -- min_P dt_P replaced by the FIRST cell's dt
# ---------------------------------------------------------------------------------------------
sed -i 's|  for (const Real v : dt) smallest = std::min(smallest, v);|  smallest = dt.empty() ? smallest : dt[0];  // N7|' "$SRC"
control N7 "min_P dt_P replaced by the first cell's dt" "U2" "K1 K3"

# ---------------------------------------------------------------------------------------------
echo "### final restore verification ###"
FINAL=$(sha256sum "$SRC" | awk '{print $1}')
echo "  $SRC sha256 : $FINAL"
echo "  pristine     : $PRISTINE"
if [ "$FINAL" = "$PRISTINE" ]; then echo "  RESTORED: yes"; else echo "  *** NOT RESTORED ***"; overall=$((overall+1)); fi
echo "  rebuilding the pristine tree so no planted binary survives"
cmake --build build/qual-final --target CFDCompressibleTests CFDDensityBasedCanonicalTests -j 16 \
  > "$WORK/build_restore.log" 2>&1
echo "  rebuild exit: $?"
rm -f "$WORK/last_run.log"
echo ""
echo "CONTROLS WITH AN UNEXPECTED RESULT: $overall   (must be 0)"
if [ "$overall" -eq 0 ]; then exit 0; else exit 1; fi

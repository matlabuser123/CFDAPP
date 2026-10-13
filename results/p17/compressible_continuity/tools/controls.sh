#!/usr/bin/env bash
# P17.3 Group N -- non-vacuity controls. Each plants ONE defect, proves the intended
# criteria FAIL, then restores and verifies the restore by sha256.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_continuity
B=build/qual-final
CONT=src/compressible/CompressibleContinuity.cpp
MF=src/compressible/CompressibleMassFlux.cpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs" "$P17/controls"

cp "$CONT" "$P17/controls/CompressibleContinuity.cpp.pristine"
cp "$MF" "$P17/controls/CompressibleMassFlux.cpp.pristine"
CONT_SHA=$(sha256sum "$CONT" | awk '{print $1}')
MF_SHA=$(sha256sum "$MF" | awk '{print $1}')

restore() {
  cp "$P17/controls/CompressibleContinuity.cpp.pristine" "$CONT"
  cp "$P17/controls/CompressibleMassFlux.cpp.pristine" "$MF"
  local a b
  a=$(sha256sum "$CONT" | awk '{print $1}')
  b=$(sha256sum "$MF" | awk '{print $1}')
  if [ "$a" = "$CONT_SHA" ] && [ "$b" = "$MF_SHA" ]; then
    echo "  RESTORE            VERIFIED by SHA-256 (both files)"
  else
    echo "  RESTORE            *** FAILED ***"
  fi
}

run_control() {
  local id="$1" desc="$2" failRx="$3" passRx="$4"
  echo "=== $id -- $desc ==="
  if ! cmake --build "$B" -j 16 > "$P17/logs/N_${id}_build.log" 2>&1; then
    echo "  PLANTED DEFECT     did not compile -- control INVALID"
    grep -aE 'error:' "$P17/logs/N_${id}_build.log" | head -4 | sed 's/^/    /'
    restore; echo ""; return
  fi
  echo "  PLANTED DEFECT     yes (compiled)"
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$failRx" --output-on-failure \
    > "$P17/logs/N_${id}_after.log" 2>&1
  local rc=$? nfail
  nfail=$(grep -aoE '[0-9]+ tests failed' "$P17/logs/N_${id}_after.log" | tail -1 | grep -oE '^[0-9]+')
  nfail=${nfail:-0}
  if [ "$rc" -ne 0 ] && [ "$nfail" -gt 0 ]; then
    echo "  CRITERION          FAILED as expected ($nfail tests)"
    echo "  DETECTION          DETECTED"
    grep -aoE '[A-Za-z]+Test\.[A-Za-z]+ .*Failed' "$P17/logs/N_${id}_after.log" \
      | grep -oE '^[A-Za-z]+Test\.[A-Za-z]+' | sort -u | head -8 | sed 's/^/      /'
  else
    echo "  CRITERION          *** DID NOT FAIL -- the criterion is VACUOUS ***"
  fi
  if [ "$passRx" != "-" ]; then
    OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$passRx" > "$P17/logs/N_${id}_stillpass.log" 2>&1
    printf '  MUST STILL PASS    -> %s\n' \
      "$(grep -aE 'tests passed|tests failed' "$P17/logs/N_${id}_stillpass.log" | tail -1)"
  fi
  restore
  echo ""
}

log=$P17/logs/07_controls.log
{
echo "P17.3 GROUP N -- non-vacuity controls"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $CONT sha256: $CONT_SHA"
echo "baseline $MF   sha256: $MF_SHA"
echo ""

# ---- N1: drop the density-value validation ----------------------------------
sed -i 's|if (!std::isfinite(densityOld\[i\]) \|\| !(densityOld\[i\] > 0.0)) {|if (false) {  // N1|' "$CONT"
sed -i 's|if (!std::isfinite(densityNew\[i\]) \|\| !(densityNew\[i\] > 0.0)) {|if (false) {  // N1|' "$CONT"
run_control N1 "drop the density-value validation" \
  'CompressibleContinuityTest.RejectsNonFiniteOrNonPositiveDensity|CompressibleContinuityTest.RejectionMessagesNameTheOffendingIndex' \
  'CompressibleContinuityTest.RejectsNonFiniteMassFlux'

# ---- N2: drop the mass-flux finiteness validation ---------------------------
sed -i 's|if (!std::isfinite(massFlux\[f\])) {|if (false) {  // N2|' "$CONT"
run_control N2 "drop the mass-flux finiteness validation" \
  'CompressibleContinuityTest.RejectsNonFiniteMassFlux|CompressibleContinuityTest.RejectionMessagesNameTheOffendingIndex' \
  'CompressibleContinuityTest.RejectsNonFiniteOrNonPositiveDensity'

# ---- N3: globalNetFlux sums ALL faces, not only boundary faces ---------------
perl -0777 -pi -e 's/  for \(const auto& patch : mesh\.boundaryPatches\(\)\) \{\n    for \(const Index faceId : patch\.faceIds\(\)\) \{\n      result\.globalNetFlux \+= massFlux\[faceId\];\n    \}\n  \}/  \/\/ N3\n  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {\n    result.globalNetFlux += massFlux[faceId];\n  }/' "$CONT"
run_control N3 "globalNetFlux sums ALL faces, not only the boundary" \
  'CompressibleContinuityTest.GlobalNetFluxSumsBoundaryFacesOnly|CompressibleContinuityTest.InteriorFaceContributionsCancelGlobally|CompressibleContinuityTest.TransientIntegralMassConservationHolds' \
  '-'

# ---- N4: face density collapsed to a global constant ------------------------
sed -i 's|faceDensity\[faceId\] = cfd::discretization::interpolateInternalFace(mesh, face, density);|faceDensity[faceId] = density[0];  // N4|' "$MF"
run_control N4 "internal face density is a global constant (a rho*div(U) implementation)" \
  'CompressibleMassFluxTest.RhoTimesUConstantConvergesAtSecondOrder|CompressibleMassFluxTest.InternalFaceUsesArithmeticMeanDensity|CompressibleMassFluxTest.RowVaryingDensityConservesMassAndScalesFluxPerRow' \
  '-'

# ---- N5: flip the neighbour sign --------------------------------------------
sed -i 's|result.cellImbalance\[\*face.neighbor()\] -= flux;|result.cellImbalance[*face.neighbor()] += flux;  // N5|' "$CONT"
run_control N5 "flip the neighbour sign (+F instead of -F)" \
  'CompressibleContinuityTest.UniformDivergenceFreeFluxGivesExactlyZeroImbalance|CompressibleContinuityTest.InteriorFaceContributionsCancelGlobally|CompressibleContinuityTest.ReducesToTheSteadyEvaluatorWhenDensityIsUnchanged|CompressibleContinuityTest.SteadyStateWithBalancedFluxGivesZeroImbalance' \
  '-'

# ---- N6: drop the V_P/dt factor from the time term ---------------------------
sed -i 's|result.cellImbalance\[id\] = (densityNew\[id\] - densityOld\[id\]) \* cell.volume() / dt;|result.cellImbalance[id] = (densityNew[id] - densityOld[id]);  // N6|' "$CONT"
run_control N6 "drop V_P/dt from the transient term" \
  'CompressibleContinuityTest.TimeOnlyImbalanceWithZeroFlux|CompressibleContinuityTest.CombinedTimeAndFluxHandDerived' \
  'CompressibleContinuityTest.QuiescentClosedBoxConservesExactly'

# ---- restored, and proved back at baseline ------------------------------------
echo "=== restored, and proved back at baseline ==="
sha256sum "$CONT" "$MF" | sed 's/^/  /'
echo "  expected: $CONT_SHA"
echo "            $MF_SHA"
echo "  N-markers left: $(grep -c '// N[1-6]' "$CONT" "$MF" | awk -F: '{s+=$2} END{print s}')  (0 required)"
cmake --build "$B" -j 16 > "$P17/logs/N_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'CompressibleContinuityTest|CompressibleMassFluxTest' \
  > "$P17/logs/N_restore.log" 2>&1
echo "  at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/N_restore.log" | tail -1)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

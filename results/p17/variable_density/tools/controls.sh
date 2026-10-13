#!/usr/bin/env bash
# P17.1 Group N -- non-vacuity controls. Each plants ONE defect, proves the intended
# criteria FAIL, then restores and verifies the restore by sha256.
#
# Gate: acceptance_gate.md section 7, sha256
# 876466e2c97fd8db93e1dd96ac834a18a0c1c99ee8d381ab45b324b6dedc39ff
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/variable_density
B=build/qual-final
SRC=src/compressible/CompressibleMassFlux.cpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs" "$P17/controls"

PRISTINE=$P17/controls/CompressibleMassFlux.cpp.pristine
cp "$SRC" "$PRISTINE"
BASE_SHA=$(sha256sum "$SRC" | awk '{print $1}')

restore() {
  cp "$PRISTINE" "$SRC"
  local now
  now=$(sha256sum "$SRC" | awk '{print $1}')
  if [ "$now" = "$BASE_SHA" ]; then
    echo "  RESTORE            VERIFIED by SHA-256"
  else
    echo "  RESTORE            *** FAILED -- $now != $BASE_SHA ***"
  fi
}

# run_control <id> <description> <expected-to-fail test regex> <must-still-pass regex or ->
run_control() {
  local id="$1" desc="$2" failRx="$3" passRx="$4"
  echo "=== $id -- $desc ==="
  if ! cmake --build "$B" -j 16 > "$P17/logs/N_${id}_build.log" 2>&1; then
    echo "  PLANTED DEFECT     did not compile -- control INVALID"
    grep -aE 'error:' "$P17/logs/N_${id}_build.log" | head -4 | sed 's/^/    /'
    restore
    echo ""
    return
  fi
  echo "  PLANTED DEFECT     yes (compiled)"
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -R "$failRx" --output-on-failure \
    > "$P17/logs/N_${id}_after.log" 2>&1
  local rc=$?
  local nfail
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
    OMP_NUM_THREADS=1 ctest --test-dir "$B" -R "$passRx" > "$P17/logs/N_${id}_stillpass.log" 2>&1
    printf '  MUST STILL PASS    %s -> %s\n' "$passRx" \
      "$(grep -aE 'tests passed|tests failed' "$P17/logs/N_${id}_stillpass.log" | tail -1)"
  fi
  restore
  echo ""
}

log=$P17/logs/06_controls.log
{
echo "P17.1 GROUP N -- non-vacuity controls"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $SRC sha256: $BASE_SHA"
echo ""

# ---- N1: remove the density-value validation entirely --------------------------
sed -i 's|if (!std::isfinite(density\[i\]) \|\| !(density\[i\] > 0.0)) {|if (false) {  // N1|' "$SRC"
run_control N1 "remove the density-value validation entirely" \
  'CompressibleMassFluxTest.Rejects|CompressibleMassFluxTest.FaceDensityEvaluationRejects' '-'

# ---- N2: finiteness only, drop the positivity clause -------------------------
# THE SHARPEST CONTROL: proves the > 0 clause is what detects the SILENT finite cases
# (rho = -2 giving face rho_f = 0, and rho = 0 giving a plausible flux), not finiteness.
sed -i 's|if (!std::isfinite(density\[i\]) \|\| !(density\[i\] > 0.0)) {|if (!std::isfinite(density[i])) {  // N2|' "$SRC"
run_control N2 "validate finiteness only -- drop the > 0 clause" \
  'CompressibleMassFluxTest.RejectsNegativeCellDensity|CompressibleMassFluxTest.RejectsZeroCellDensity|CompressibleMassFluxTest.FaceDensityEvaluationRejects' \
  'CompressibleMassFluxTest.RejectsNonFiniteCellDensity'

# ---- N3: >= 0 instead of > 0 --------------------------------------------------
sed -i 's|!(density\[i\] > 0.0)|!(density[i] >= 0.0)  /* N3 */|' "$SRC"
run_control N3 "accept rho >= 0 instead of rho > 0" \
  'CompressibleMassFluxTest.RejectsZeroCellDensity' \
  'CompressibleMassFluxTest.RejectsNegativeCellDensity|CompressibleMassFluxTest.RejectsNonFiniteCellDensity'

# ---- N4: internal face density becomes a global constant ---------------------
sed -i 's|faceDensity\[faceId\] = cfd::discretization::interpolateInternalFace(mesh, face, density);|faceDensity[faceId] = density[0];  // N4|' "$SRC"
run_control N4 "internal face density is a global constant, not an interpolation" \
  'CompressibleMassFluxTest.InternalFaceUsesArithmeticMeanDensity|CompressibleMassFluxTest.RowVaryingDensityConservesMassAndScalesFluxPerRow|CompressibleMassFluxTest.FaceDensityIsRecomputedPerCallNotCached' \
  '-'

# ---- N5: collapse EVERY face density to one reference constant ----------------
# The faithful analogue of using a single material density everywhere.
sed -i 's|faceDensity\[faceId\] = thermodynamics.density(faceAbsolutePressure, faceTemperature);|faceDensity[faceId] = thermodynamics.density(referencePressure, temperature[0]);  // N5|' "$SRC"
sed -i 's|faceDensity\[faceId\] = cfd::discretization::interpolateInternalFace(mesh, face, density);|faceDensity[faceId] = thermodynamics.density(referencePressure, temperature[0]);  // N5|' "$SRC"
run_control N5 "collapse every face density to one reference constant" \
  'CompressibleMassFluxTest.InternalFaceUsesArithmeticMeanDensity|CompressibleMassFluxTest.OutletBoundaryUsesEosEvaluatedPressureNotOwnerDensity|CompressibleMassFluxTest.RowVaryingDensityConservesMassAndScalesFluxPerRow' \
  'CompressibleMassFluxTest.UniformDensityMatchesIncompressibleMassFluxExactly'

# ---- restored and proved back at baseline ------------------------------------
echo "=== restored, and proved back at baseline ==="
sha256sum "$SRC" | sed 's/^/  /'
echo "  expected: $BASE_SHA"
echo "  N-markers left: $(grep -c '// N[1-5]\|/\* N3 \*/' "$SRC")  (0 required)"
cmake --build "$B" -j 16 > "$P17/logs/N_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -R 'CompressibleMassFluxTest' > "$P17/logs/N_restore.log" 2>&1
echo "  mass-flux tests at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/N_restore.log" | tail -1)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

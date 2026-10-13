#!/usr/bin/env bash
# P17.9 -- exactly which frozen Group V expectations does V2 (HLL) break, now that A1 exists?
# The frozen gate's V2 row names W3, X2 and S3. Measured here per test, and reported as measured.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/shock_validation
SRC=src/compressible/DensityBasedSolver.cpp
W=$P17/controls
BIN=build/qual-final/tests/integration/compressible/CFDShockValidationTests
cp "$SRC" "$W/v2.pristine"
PR=$(sha256sum "$SRC" | awk '{print $1}')

sed -i 's|hllc = hllcFluxChecked(faceState.left, faceState.right, normal, gamma);|hllc = hllFluxForControlOnly(faceState.left, faceState.right, normal, gamma);|' "$SRC"
[ "$(sha256sum "$SRC" | awk '{print $1}')" != "$PR" ] || { echo "plant did not apply"; exit 2; }
cmake --build build/qual-final --target CFDShockValidationTests -j 16 > /dev/null 2>&1

echo "V2 (HLL, contact wave deleted) against each frozen expectation:"
for t in SodShockTubeTest.WX_ThreeStructuresAreIdentifiedByMeasurementAndPositionedCorrectly \
         SodShockTubeTest.S_PlateauStatesMatchTheExactSolution \
         ContactDiscontinuityTest.C_StationaryAndMovingContactsBehaveCorrectly; do
  if OMP_NUM_THREADS=1 "$BIN" --gtest_filter="$t" > "$W/v2_$t.log" 2>&1; then r=PASS; else r=FAIL; fi
  printf '  %-62s %s\n' "${t#*.}" "$r"
  grep -aE 'W3 contact|A1|smeared width|rho_star_left|contact  ' "$W/v2_$t.log" | head -4 | sed 's/^/      /'
done

cp "$W/v2.pristine" "$SRC"
NOW=$(sha256sum "$SRC" | awk '{print $1}')
[ "$NOW" = "$PR" ] || { echo "*** RESTORE FAILED"; exit 3; }
cmake --build build/qual-final --target CFDShockValidationTests -j 16 > /dev/null 2>&1
rm -f "$W/v2.pristine"
echo "restored and rebuilt: $NOW"

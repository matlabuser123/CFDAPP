#!/usr/bin/env bash
# P17.9 DIAGNOSTIC -- quantify what the gate's Group V FAILED to measure.
#
# V2 (HLL, contact wave deleted) passed every frozen criterion. G7's own text says that means the
# gate is not measuring what HLLC is for. This script measures the quantity that DOES discriminate
# -- the contact's smeared width in cells -- for HLLC, HLL and Rusanov, so the size of the gap is a
# number rather than an assertion. It changes no criterion and decides nothing.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/shock_validation
SRC=src/compressible/DensityBasedSolver.cpp
W=$P17/controls
BIN=build/qual-final/tests/integration/compressible/CFDShockValidationTests
cp "$SRC" "$W/width.pristine"
PR=$(sha256sum "$SRC" | awk '{print $1}')

measure() {
  cmake --build build/qual-final --target CFDShockValidationTests -j 16 > /dev/null 2>&1 || {
    echo "  build failed"; return; }
  OMP_NUM_THREADS=1 "$BIN" --gtest_filter=SodShockTubeTest.WX_ThreeStructuresAreIdentifiedByMeasurementAndPositionedCorrectly \
    > "$W/width_$1.log" 2>&1
  local w; w=$(grep -aoE 'shock [0-9]+ cells, contact [0-9]+ cells' "$W/width_$1.log" | tail -1)
  OMP_NUM_THREADS=1 "$BIN" --gtest_filter=ContactDiscontinuityTest.C_StationaryAndMovingContactsBehaveCorrectly \
    > "$W/width_${1}_contact.log" 2>&1
  local s m
  s=$(grep -aoE 'width [0-9]+ cells' "$W/width_${1}_contact.log" | head -1)
  m=$(grep -aoE 'width [0-9]+ cells' "$W/width_${1}_contact.log" | tail -1)
  printf '  %-10s  Sod: %-36s  stationary contact: %-16s  moving contact: %s\n' "$1" "${w:-n/a}" "${s:-n/a}" "${m:-n/a}"
}

echo "CONTACT SMEARING -- the quantity that separates HLLC from a scheme without a contact wave"
echo "date: $(date -u +%FT%TZ)"
echo ""
measure HLLC

sed -i 's|hllc = hllcFluxChecked(faceState.left, faceState.right, normal, gamma);|hllc = hllFluxForControlOnly(faceState.left, faceState.right, normal, gamma);|' "$SRC"
[ "$(sha256sum "$SRC" | awk '{print $1}')" != "$PR" ] || { echo "HLL plant did not apply"; exit 2; }
measure HLL
cp "$W/width.pristine" "$SRC"

python3 - <<'PLANT'
p = "src/compressible/DensityBasedSolver.cpp"
s = open(p, encoding="utf-8").read()
old = "      hllc = hllcFluxChecked(faceState.left, faceState.right, normal, gamma);"
new = """      {
        const PrimitiveState& L = faceState.left;
        const PrimitiveState& R = faceState.right;
        const Real unL = hllc_detail::dotHD(L.velocity, normal);
        const Real unR = hllc_detail::dotHD(R.velocity, normal);
        const Real smax = std::max(std::abs(unL) + L.speedOfSound, std::abs(unR) + R.speedOfSound);
        const FluxVector fL = eulerFlux(L, normal);
        const FluxVector fR = eulerFlux(R, normal);
        const ConservedState qL = toConserved(L);
        const ConservedState qR = toConserved(R);
        hllc.valid = true;
        hllc.flux.mass = 0.5 * (fL.mass + fR.mass) - 0.5 * smax * (qR.density - qL.density);
        hllc.flux.momentum.x = 0.5 * (fL.momentum.x + fR.momentum.x) -
                               0.5 * smax * (qR.momentumDensity.x - qL.momentumDensity.x);
        hllc.flux.momentum.y = 0.5 * (fL.momentum.y + fR.momentum.y) -
                               0.5 * smax * (qR.momentumDensity.y - qL.momentumDensity.y);
        hllc.flux.momentum.z = 0.5 * (fL.momentum.z + fR.momentum.z) -
                               0.5 * smax * (qR.momentumDensity.z - qL.momentumDensity.z);
        hllc.flux.energy = 0.5 * (fL.energy + fR.energy) -
                           0.5 * smax * (qR.totalEnergyDensity - qL.totalEnergyDensity);
      }"""
assert old in s
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
PLANT
measure Rusanov

cp "$W/width.pristine" "$SRC"
NOW=$(sha256sum "$SRC" | awk '{print $1}')
echo ""
echo "restored: $NOW"
echo "pristine: $PR"
[ "$NOW" = "$PR" ] || { echo "*** RESTORE FAILED"; exit 3; }
cmake --build build/qual-final --target CFDShockValidationTests -j 16 > /dev/null 2>&1
rm -f "$W/width.pristine"
echo "rebuilt from pristine source."

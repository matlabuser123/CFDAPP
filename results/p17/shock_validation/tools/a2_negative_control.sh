#!/usr/bin/env bash
# AMENDMENT A2 -- the negative control its derivation demands.
#
# The derivation says the rarefaction-head position error scales as the diffusion length,
#     err ~ sigma = sqrt(2 D t),   D ~ s dx / 2,
# so it is proportional to sqrt(D). Rusanov is strictly MORE diffusive than HLLC (it replaces the
# wave structure with a single maximum-speed dissipation), therefore the derivation PREDICTS a
# LARGER head error for Rusanov at the same mesh.
#
# That is a falsifiable prediction about a quantity the amended criterion gates. If Rusanov gave the
# same or a smaller head error, the criterion would not be measuring numerical diffusion and the
# derivation behind A2 would be wrong.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/shock_validation
SRC=src/compressible/DensityBasedSolver.cpp
W=$P17/controls
BIN=build/qual-final/tests/integration/compressible/CFDShockValidationTests
TEST=SodShockTubeTest.WX_ThreeStructuresAreIdentifiedByMeasurementAndPositionedCorrectly
cp "$SRC" "$W/a2.pristine"
PR=$(sha256sum "$SRC" | awk '{print $1}')

head_err() {
  cmake --build build/qual-final --target CFDShockValidationTests -j 16 > /dev/null 2>&1
  OMP_NUM_THREADS=1 "$BIN" --gtest_filter="$TEST" > "$W/a2_$1.log" 2>&1 || true
  grep -aE 'rarefaction head' "$W/a2_$1.log" | tail -1 | awk '{printf "%-12s %s\n", "'"$1"'", $0}'
}

echo "AMENDMENT A2 NEGATIVE CONTROL -- head error must GROW with numerical diffusion"
echo "date: $(date -u +%FT%TZ)"
echo ""
head_err HLLC

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
head_err Rusanov

cp "$W/a2.pristine" "$SRC"
NOW=$(sha256sum "$SRC" | awk '{print $1}')
[ "$NOW" = "$PR" ] || { echo "*** RESTORE FAILED"; exit 3; }
cmake --build build/qual-final --target CFDShockValidationTests -j 16 > /dev/null 2>&1
rm -f "$W/a2.pristine"
echo ""
echo "restored and rebuilt: $NOW"

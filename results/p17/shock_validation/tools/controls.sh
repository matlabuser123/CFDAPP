#!/usr/bin/env bash
# P17.9 -- the Group V non-vacuity controls that the FROZEN G7 gate demands by name:
#
#   "a first-order upwind scheme WITHOUT the Riemann flux must FAIL this gate. If it passes, the
#    gate is not measuring shock capturing. A second, discriminating case is required beyond Sod:
#    Sod alone can be passed by an over-diffusive scheme that smears everything to roughly the
#    right place."
#
# V1  RUSANOV / local Lax-Friedrichs -- the wave structure replaced by one maximum-speed
#     dissipation term. NO Riemann solver at all. This is G7's required control.
# V2  HLL -- P17.7's hllFluxForControlOnly, i.e. HLLC with the CONTACT WAVE DELETED and nothing
#     else changed. This is G7's "second, discriminating case": it separates "the gate measures
#     shock capturing" from "the gate measures the contact wave HLLC exists to restore".
#
# V3 and V4 (a shifted profile, and a jumpless ramp) test the DETECTOR rather than the solver and
# live inside the test suite itself, where they run on every invocation.
#
# Each plant changes production source, is verified to have CHANGED THE FILE'S SHA-256 before its
# results are trusted, and is restored with the hash re-checked. The script ABORTS if a restore
# does not match, so a plant cannot leak into the regression.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/shock_validation
SRC=src/compressible/DensityBasedSolver.cpp
WORK=$P17/controls
mkdir -p "$WORK"
BIN=build/qual-final/tests/integration/compressible/CFDShockValidationTests

cp "$SRC" "$WORK/DensityBasedSolver.cpp.pristine"
PRISTINE=$(sha256sum "$SRC" | awk '{print $1}')
echo "P17.9 NON-VACUITY CONTROLS"
echo "date: $(date -u +%FT%TZ)"
echo "pristine $SRC sha256: $PRISTINE"
echo ""

restore() {
  cp "$WORK/DensityBasedSolver.cpp.pristine" "$SRC"
  local now
  now=$(sha256sum "$SRC" | awk '{print $1}')
  if [ "$now" != "$PRISTINE" ]; then
    echo "*** RESTORE FAILED -- sha $now != $PRISTINE. ABORTING."
    exit 3
  fi
}

run_one() {  # run_one <gtest filter> -> PASS / FAIL
  if OMP_NUM_THREADS=1 "$BIN" --gtest_filter="$1" > "$WORK/last.log" 2>&1; then
    echo "PASS"
  else
    echo "FAIL"
  fi
}

overall=0

control() {  # control <id> <desc> <must-break filters...> -- then <must-pass filters...>
  local id="$1" desc="$2"; shift 2
  local now
  now=$(sha256sum "$SRC" | awk '{print $1}')
  echo "--- $id: $desc"
  if [ "$now" = "$PRISTINE" ]; then
    echo "    *** THE PLANT DID NOT APPLY -- every result below would be vacuous."
    overall=$((overall+1)); restore; return
  fi
  echo "    planted sha256: $now"
  if ! cmake --build build/qual-final --target CFDShockValidationTests -j 16 \
       > "$WORK/build_$id.log" 2>&1; then
    echo "    the plant does not COMPILE -- a failed control, not a detection"
    grep -aE '(^|[^a-zA-Z])error:' "$WORK/build_$id.log" | head -4 | sed 's/^/      /'
    overall=$((overall+1)); restore; return
  fi
  local phase="break"
  local bad=0
  for f in "$@"; do
    if [ "$f" = "--" ]; then phase="pass"; continue; fi
    local r; r=$(run_one "$f")
    if [ "$phase" = "break" ]; then
      if [ "$r" = "FAIL" ]; then printf '    MUST BREAK  %-52s %s   detected\n' "$f" "$r"
      else printf '    MUST BREAK  %-52s %s   *** NOT DETECTED ***\n' "$f" "$r"; bad=$((bad+1)); fi
    else
      if [ "$r" = "PASS" ]; then printf '    must pass   %-52s %s\n' "$f" "$r"
      else printf '    must pass   %-52s %s   *** COLLATERAL ***\n' "$f" "$r"; bad=$((bad+1)); fi
    fi
  done
  [ "$bad" -eq 0 ] || overall=$((overall+1))
  restore
  echo "    restored, sha256 verified"
  echo ""
}

# ---------------------------------------------------------------------------------------------
# V1 -- RUSANOV / local Lax-Friedrichs. No Riemann structure whatsoever.
# ---------------------------------------------------------------------------------------------
python3 - <<'PLANT'
p = "src/compressible/DensityBasedSolver.cpp"
s = open(p, encoding="utf-8").read()
old = "      hllc = hllcFluxChecked(faceState.left, faceState.right, normal, gamma);"
new = """      {  // V1 RUSANOV PLANT
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
assert old in s, "V1 anchor not found"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
PLANT
control V1 "Rusanov / local Lax-Friedrichs -- NO Riemann solver" \
  'ContactDiscontinuityTest.C_StationaryAndMovingContactsBehaveCorrectly' \
  'SodShockTubeTest.S_PlateauStatesMatchTheExactSolution' \
  -- \
  'ShockConservationTest.C_ClosedShockTubeConservesMassAndEnergy'

# ---------------------------------------------------------------------------------------------
# V2 -- HLL: HLLC with the CONTACT WAVE deleted, nothing else changed.
# ---------------------------------------------------------------------------------------------
sed -i 's|hllc = hllcFluxChecked(faceState.left, faceState.right, normal, gamma);|hllc = hllFluxForControlOnly(faceState.left, faceState.right, normal, gamma);  // V2|' "$SRC"
# THE FROZEN GATE'S V2 ROW PREDICTED W3, X2 AND S3 WOULD BREAK. THEY DO NOT, and that is a finding
# about the frozen row rather than about the plant. Measured in logs/12_v2_detail.log:
#
#   Sod contact smeared width   HLLC 16 cells    HLL 18 cells      -> 12 % apart, indistinguishable
#   Sod contact position (X2)   HLL 1.28 dx      limit 2 dx        -> passes
#   W3 |dp|/p*                  HLL 1.75e-03     limit 5e-02       -> passes
#   S3 rho*_L error             HLL 9.03e-03     limit 1.61e-02    -> passes
#
# On Sod the contact is ALREADY heavily smeared by both schemes, and the star states are set by the
# shock and the fan rather than by the contact's sharpness -- so no Sod-based criterion can separate
# HLLC from HLL. The STATIONARY contact can: 0 cells against 21. That is criterion A1, and it is the
# reason A1 was ADDED rather than a Sod tolerance being tightened to force a detection.
control V2 "HLL -- HLLC with the contact wave DELETED" \
  'ContactDiscontinuityTest.C_StationaryAndMovingContactsBehaveCorrectly' \
  -- \
  'NormalShockTest.N_JumpRelationsAndRankineHugoniot' \
  'SodShockTubeTest.S_PlateauStatesMatchTheExactSolution'

echo "### final restore verification ###"
FINAL=$(sha256sum "$SRC" | awk '{print $1}')
echo "  $SRC sha256 : $FINAL"
echo "  pristine     : $PRISTINE"
if [ "$FINAL" = "$PRISTINE" ]; then echo "  RESTORED: yes"; else echo "  *** NOT RESTORED ***"; overall=$((overall+1)); fi
echo "  rebuilding from the pristine source so no planted binary survives"
cmake --build build/qual-final --target CFDShockValidationTests -j 16 > "$WORK/build_restore.log" 2>&1
echo "  rebuild exit: $?"
rm -f "$WORK/last.log"
echo ""
echo "CONTROLS WITH AN UNEXPECTED RESULT: $overall   (must be 0)"
if [ "$overall" -eq 0 ]; then exit 0; else exit 1; fi

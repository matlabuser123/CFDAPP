#!/usr/bin/env bash
# P17.7 Group V -- non-vacuity controls.
#
# V1 and V2 are NOT in this script, deliberately: they are PERMANENT TESTS in the suite, because both
# defects exist as real, callable code rather than as hypotheticals --
#   V1  WaveSpeedEstimator::Davis            -> HllcWaveSpeedTest.W4_NonVacuity_DavisFailsTheSame...
#   V2  hllFluxForControlOnly() (HLL)        -> HllcContactTest.T4_NonVacuity_DeletingTheContact...
#                                            -> HllcGpuEquivalenceTest.V2_...OnDeviceToo
# A permanent test cannot silently rot the way a script that nobody reruns can, so where the
# alternative implementation is worth keeping, that is the better control. V3-V7 are genuine defects
# with no reason to exist in the codebase, so they are planted and removed.
#
# THE PROBE SET IS THE WHOLE PHASE SUITE: -R 'Hllc' -- 33 CPU tests plus 4 GPU tests. P17.4's N6 was
# wrongly called vacuous because its regex excluded the file holding an existing detector.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/hllc
B=build/qual-final
H=include/cfd/compressible/HllcFlux.hpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs" "$P17/controls"

cp "$H" "$P17/controls/HllcFlux.hpp.pristine"
H_SHA=$(sha256sum "$H" | awk '{print $1}')

restore() {
  cp "$P17/controls/HllcFlux.hpp.pristine" "$H"
  local a
  a=$(sha256sum "$H" | awk '{print $1}')
  if [ "$a" = "$H_SHA" ]; then
    echo "  RESTORE            VERIFIED by SHA-256"
  else
    echo "  RESTORE            *** FAILED ***"
  fi
}

# A sed that does not match plants NOTHING, and the control then reports a spurious "VACUOUS" -- a
# false accusation against a sound criterion. P17.6 hit exactly that. Every plant is verified by its
# own marker before the control runs.
verify_planted() {
  local id="$1"
  if ! grep -q "// $id" "$H"; then
    echo "=== $id -- *** PLANT DID NOT APPLY: no // $id marker. ABORTING. ***"
    restore
    exit 1
  fi
}

run_control() {
  local id="$1" desc="$2"
  echo "=== $id -- $desc ==="
  if ! cmake --build "$B" --target CFDCompressibleTests CFDGpuTests -j 16 \
       > "$P17/logs/V_${id}_build.log" 2>&1; then
    echo "  PLANTED DEFECT     did not compile -- control INVALID"
    grep -aE 'error:' "$P17/logs/V_${id}_build.log" | head -4 | sed 's/^/    /'
    restore; echo ""; return
  fi
  echo "  PLANTED DEFECT     yes (compiled)"
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'Hllc' --output-on-failure \
    > "$P17/logs/V_${id}_after.log" 2>&1
  local rc=$? nfail
  nfail=$(grep -aoE '[0-9]+ tests failed' "$P17/logs/V_${id}_after.log" | tail -1 | grep -oE '^[0-9]+')
  nfail=${nfail:-0}
  if [ "$rc" -ne 0 ] && [ "$nfail" -gt 0 ]; then
    echo "  CRITERION          FAILED as expected ($nfail tests)"
    echo "  DETECTION          DETECTED, by:"
    grep -aE '\(Failed\)' "$P17/logs/V_${id}_after.log" \
      | sed -E 's/.*[0-9]+ - //; s/ \(Failed\)//' | sort -u | sed 's/^/      /'
  else
    echo "  CRITERION          *** DID NOT FAIL -- the criterion is VACUOUS ***"
    echo "  (suspect the PROBE SET first, then whether a defect was actually planted)"
  fi
  restore
  echo ""
}

log=$P17/logs/06_controls.log
{
echo "P17.7 GROUP V -- non-vacuity controls"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $H sha256: $H_SHA"
echo "probe set: -R 'Hllc' -- the whole phase suite, CPU and GPU"
echo ""

set -e

# ---- V3: tangential velocity taken from the OPPOSITE side in the star state ----------------
# The contact must carry tangential velocity through UNCHANGED. Swapping the side it comes from is
# invisible for any case where the tangential components happen to match, which is why the suite
# needs a deliberate shear case (T3) at all.
python3 - "$H" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
old = """  const Real vStarLx = l.velocity.x + (sStar - unL) * unitNormal.x;
  const Real vStarLy = l.velocity.y + (sStar - unL) * unitNormal.y;
  const Real vStarLz = l.velocity.z + (sStar - unL) * unitNormal.z;"""
new = """  const Real vStarLx = r.velocity.x + (sStar - unL) * unitNormal.x;  // V3
  const Real vStarLy = r.velocity.y + (sStar - unL) * unitNormal.y;  // V3
  const Real vStarLz = r.velocity.z + (sStar - unL) * unitNormal.z;  // V3"""
assert old in s, "V3 anchor missing"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new, 1))
PY
set +e
verify_planted V3
run_control V3 "the star tangential velocity comes from the OPPOSITE side"

# ---- V4: S* replaced by the arithmetic mean of the normal velocities -----------------------
set -e
python3 - "$H" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
old = "  const Real sStar = (r.pressure - l.pressure + qL * unL - qR * unR) / denominator;"
new = "  const Real sStar = 0.5 * (unL + unR);  // V4"
assert old in s, "V4 anchor missing"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new, 1))
PY
set +e
verify_planted V4
run_control V4 "S* is the arithmetic mean of u_nL and u_nR, not Toro's contact speed"

# ---- V5: the pressure term dropped from the physical flux ----------------------------------
set -e
python3 - "$H" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
old = """  f.momentum.x = rhoUn * s.velocity.x + s.pressure * n.x;
  f.momentum.y = rhoUn * s.velocity.y + s.pressure * n.y;
  f.momentum.z = rhoUn * s.velocity.z + s.pressure * n.z;"""
new = """  f.momentum.x = rhoUn * s.velocity.x;  // V5
  f.momentum.y = rhoUn * s.velocity.y;  // V5
  f.momentum.z = rhoUn * s.velocity.z;  // V5"""
assert old in s, "V5 anchor missing"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new, 1))
PY
set +e
verify_planted V5
run_control V5 "the p*n term is dropped from the physical Euler flux"

# ---- V6: the unit-normal check removed ----------------------------------------------------
# Section 20 warns against mixing a unit normal with an area vector. With the check gone, an area
# vector is silently accepted and the wave speeds scale with it.
set -e
python3 - "$H" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
old = "  if (absR(lengthSquared - 1.0) > kUnitTolerance) return StateValidity::NonUnitNormal;"
new = "  (void)lengthSquared;  // V6\n  if (false) return StateValidity::NonUnitNormal;"
assert old in s, "V6 anchor missing"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new, 1))
PY
set +e
verify_planted V6
run_control V6 "the unit-normal validation is removed, so an AREA VECTOR is accepted"

# ---- V7: the star-density denominator uses (S_K - u_nK) instead of (S_K - S*) --------------
set -e
python3 - "$H" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
old = """  const Real rhoStarL = l.density * (sL - unL) / (sL - sStar);
  const Real rhoStarR = r.density * (sR - unR) / (sR - sStar);"""
new = """  const Real rhoStarL = l.density * (sL - unL) / (sL - unL);  // V7
  const Real rhoStarR = r.density * (sR - unR) / (sR - unR);  // V7"""
assert old in s, "V7 anchor missing"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new, 1))
PY
set +e
verify_planted V7
run_control V7 "the star density divides by (S_K - u_nK) instead of (S_K - S*)"

echo "=== restored, and proved back at baseline ==="
sha256sum "$H" | sed 's/^/  /'
echo "  expected: $H_SHA"
echo "  V-markers left: $(grep -c '// V[0-9]' "$H") (0 required)"
cmake --build "$B" --target CFDCompressibleTests CFDGpuTests -j 16 \
  > "$P17/logs/V_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'Hllc' > "$P17/logs/V_restore.log" 2>&1
echo "  at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/V_restore.log" | tail -1)"
rm -f "$P17/controls"/*.pristine
echo ""

echo "=== V1 and V2 -- PERMANENT tests, not plants, and here is why ==="
echo "  Both defects exist as real callable code that is worth keeping:"
echo "    V1  WaveSpeedEstimator::Davis -- the estimator the authorization SUGGESTED, measured to"
echo "        fail the signal-bounding criterion W4 in 8 of 14 cases against the exact solver."
echo "    V2  hllFluxForControlOnly()   -- HLLC with the contact wave deleted, i.e. HLL, which the"
echo "        frozen G6 criterion itself names as the non-vacuity backbone."
echo "  Both are asserted by permanent tests (W4_NonVacuity_Davis..., T4_NonVacuity_Deleting...,"
echo "  and V2_...OnDeviceToo), so they are re-checked by every future regression instead of living"
echo "  in a script that may never be run again."
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

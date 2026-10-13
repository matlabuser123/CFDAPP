#!/usr/bin/env bash
# P17.6 Group N -- non-vacuity controls.
#
# P17.6 changes NO production source, so the defects are planted in the VALIDATION HARNESS itself.
# That is the right target here: the thing being tested for vacuity is whether the comparison
# actually measures the compressible-to-incompressible distance, and the harness is what makes that
# claim.
#
# THE PROBE SET IS THE WHOLE PHASE SUITE, -R 'LowMachFamily', all 15 tests. P17.4's N6 was wrongly
# declared vacuous because its regex excluded the file holding an existing detector.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/low_mach
B=build/qual-final
T=tests/integration/compressible/test_low_mach_family.cpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs" "$P17/controls"

cp "$T" "$P17/controls/test_low_mach_family.cpp.pristine"
T_SHA=$(sha256sum "$T" | awk '{print $1}')

restore() {
  cp "$P17/controls/test_low_mach_family.cpp.pristine" "$T"
  local a
  a=$(sha256sum "$T" | awk '{print $1}')
  if [ "$a" = "$T_SHA" ]; then
    echo "  RESTORE            VERIFIED by SHA-256"
  else
    echo "  RESTORE            *** FAILED ***"
  fi
}

# A sed that does not match silently plants NOTHING and the control then reports a spurious
# "VACUOUS". Every plant is therefore VERIFIED by looking for its own marker before the control runs.
verify_planted() {
  local id="$1"
  if ! grep -q "// $id" "$T"; then
    echo "=== $id -- *** PLANT DID NOT APPLY: no // $id marker in the harness. ABORTING. ***"
    restore
    exit 1
  fi
}

run_control() {
  local id="$1" desc="$2" expect="$3"
  echo "=== $id -- $desc ==="
  if ! cmake --build "$B" --target CFDLowMachFamilyTests -j 16 > "$P17/logs/N_${id}_build.log" 2>&1; then
    echo "  PLANTED DEFECT     did not compile -- control INVALID"
    grep -aE 'error:' "$P17/logs/N_${id}_build.log" | head -4 | sed 's/^/    /'
    restore; echo ""; return
  fi
  echo "  PLANTED DEFECT     yes (compiled)"
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'LowMachFamily' --output-on-failure \
    > "$P17/logs/N_${id}_after.log" 2>&1
  local rc=$? nfail
  nfail=$(grep -aoE '[0-9]+ tests failed' "$P17/logs/N_${id}_after.log" | tail -1 | grep -oE '^[0-9]+')
  nfail=${nfail:-0}
  if [ "$expect" = "must_fail" ]; then
    if [ "$rc" -ne 0 ] && [ "$nfail" -gt 0 ]; then
      echo "  CRITERION          FAILED as expected ($nfail of 15 tests)"
      echo "  DETECTION          DETECTED, by:"
      grep -aE '\(Failed\)' "$P17/logs/N_${id}_after.log" \
        | sed -E 's/.*[0-9]+ - //; s/ \(Failed\)//' | sort -u | sed 's/^/      /'
    else
      echo "  CRITERION          *** DID NOT FAIL -- the criterion is VACUOUS ***"
      echo "  (suspect the PROBE SET first, then whether a defect was actually planted)"
    fi
  else
    # N5 is the unusual one: the WRONG criterion must PASS, which is how the trap is shown to be
    # real rather than theoretical.
    if [ "$nfail" -eq 0 ]; then
      echo "  RESULT             PASSED, as the trap predicts ($nfail failures)"
      echo "  => a criterion placed on rhoE instead of the kinetic part is satisfied by anything."
    else
      echo "  RESULT             failed ($nfail) -- the trap did NOT reproduce; investigate"
    fi
  fi
  restore
  echo ""
}

log=$P17/logs/05_controls.log
{
echo "P17.6 GROUP N -- non-vacuity controls"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $T sha256: $T_SHA"
echo "probe set: -R 'LowMachFamily' = all 15 tests of the phase"
echo ""

# ---- N1: compare the compressible solution against ITSELF ------------------
# If the reference were the compressible solution, every difference would be exactly 0 and the whole
# comparison would be measuring nothing. F3 exists to catch precisely this.
# The anchor is the post-clang-format text: the gate runs `clang-format-18 -i` on this file, which
# joined the declaration onto one line. A control whose anchor silently misses plants NOTHING and
# then reports a spurious "VACUOUS", so the python asserts and `set -e` stops the run.
set -e
python3 - "$T" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
old = "        std::sqrt(inc.velocity[i].x * inc.velocity[i].x + inc.velocity[i].y * inc.velocity[i].y);"
new = "        std::sqrt(cmp.velocity[i].x * cmp.velocity[i].x + cmp.velocity[i].y * cmp.velocity[i].y);  // N1"
assert old in s, "N1 anchor missing"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new, 1))
PY
set +e
verify_planted N1
run_control N1 "the reference is the COMPRESSIBLE solution itself, not the incompressible one" must_fail

# ---- N2: the inconsistent reference density -------------------------------
# rho_ref = 1.0 is the existing test's value; the EOS at the reference state gives 1.176624. A 15%
# density mismatch must swamp a trend whose whole range is 1e-4.
sed -i 's|const Real rhoRef = thermo.density(kReferencePressure, kTemperature);|const Real rhoRef = 1.0;  // N2|' "$T"
verify_planted N2
run_control N2 "rho_ref = 1.0 instead of the EOS-derived 1.176624 (a 15 percent mismatch)" must_fail

# ---- N3: report the REQUESTED Mach instead of the achieved one -------------
sed -i 's|L.machMax = std::max(L.machMax, machNumber(mc, soundSpeed));|L.machMax = L.inletMach;  // N3|' "$T"
verify_planted N3
run_control N3 "machMax reports the inlet Mach, i.e. the requested value, not the achieved one" must_fail

# ---- N5: gate the energy criterion on rhoE instead of the kinetic part -----
# THE TRAP the frozen G5 clause names. This control must PASS, which is the only way to show the trap
# is real: ke/e is 6-8 orders down, so an rhoE-relative criterion is satisfied by anything at all.
sed -i 's|L.kineticRelDiff = std::abs(keComp - keInc) / std::max(std::abs(keInc), 1e-300);|L.kineticRelDiff = std::abs(keComp - keInc) / std::max(std::abs(keComp / 4.832e-06), 1e-300);  // N5: normalise by rhoE-scale instead of by ke|' "$T"
verify_planted N5
run_control N5 "the energy criterion is normalised by the rhoE scale, not by the kinetic part" must_pass

echo "=== restored, and proved back at baseline ==="
sha256sum "$T" | sed 's/^/  /'
echo "  expected: $T_SHA"
echo "  N-markers left: $(grep -c '// N[0-9]' "$T") (0 required)"
cmake --build "$B" --target CFDLowMachFamilyTests -j 16 > "$P17/logs/N_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'LowMachFamily' > "$P17/logs/N_restore.log" 2>&1
echo "  at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/N_restore.log" | tail -1)"
rm -f "$P17/controls"/*.pristine
echo ""

echo "=== N4 -- verified by MEASUREMENT, not by a plant, and here is why ==="
echo "  The frozen gate lists N4 as 'drop the 1.25 * inletMach clause -> M4 becomes vacuous'."
echo "  Planting that means DELETING an assertion, and a deleted assertion always 'passes' -- so it"
echo "  is not a defect any probe can detect. It is a degenerate control."
echo "  What the clause is actually worth is its MARGIN, which is measurable:"
echo "    machMax / inletMach = 0.0041552 / 0.0028802 = 1.4427"
echo "    the clause requires                          > 1.25"
echo "  So the flow does accelerate, by 44 percent against a 25 percent bar -- a real but not"
echo "  enormous margin, and the Poiseuille peak factor of 1.5 is the physical value it approaches."
echo "  Recorded as a measurement rather than faked as a plant."
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

#!/usr/bin/env bash
# P17.5 Group N -- non-vacuity controls. Every one MUST fail.
#
# THE PROBE SET IS THE WHOLE PHASE SUITE, on purpose. P17.4's control N6 was declared vacuous only
# because its fail-regex excluded the file containing an existing detector -- a too-narrow probe, not
# a coverage gap. Here every control is run against -R 'TotalEnergy', all 34 tests, and the tests
# that fired are listed by name so a reader can see WHICH criterion caught it rather than trusting a
# count.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/total_energy
B=build/qual-final
STATE=src/compressible/TotalEnergy.cpp
EQN=src/compressible/TotalEnergyEquation.cpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs" "$P17/controls"

cp "$STATE" "$P17/controls/TotalEnergy.cpp.pristine"
cp "$EQN" "$P17/controls/TotalEnergyEquation.cpp.pristine"
STATE_SHA=$(sha256sum "$STATE" | awk '{print $1}')
EQN_SHA=$(sha256sum "$EQN" | awk '{print $1}')

restore() {
  cp "$P17/controls/TotalEnergy.cpp.pristine" "$STATE"
  cp "$P17/controls/TotalEnergyEquation.cpp.pristine" "$EQN"
  local a b
  a=$(sha256sum "$STATE" | awk '{print $1}')
  b=$(sha256sum "$EQN" | awk '{print $1}')
  if [ "$a" = "$STATE_SHA" ] && [ "$b" = "$EQN_SHA" ]; then
    echo "  RESTORE            VERIFIED by SHA-256 (both files)"
  else
    echo "  RESTORE            *** FAILED ***"
  fi
}

run_control() {
  local id="$1" desc="$2"
  echo "=== $id -- $desc ==="
  if ! cmake --build "$B" -j 16 > "$P17/logs/N_${id}_build.log" 2>&1; then
    echo "  PLANTED DEFECT     did not compile -- control INVALID"
    grep -aE 'error:' "$P17/logs/N_${id}_build.log" | head -4 | sed 's/^/    /'
    restore; echo ""; return
  fi
  echo "  PLANTED DEFECT     yes (compiled)"
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'TotalEnergy' --output-on-failure \
    > "$P17/logs/N_${id}_after.log" 2>&1
  local rc=$? nfail
  nfail=$(grep -aoE '[0-9]+ tests failed' "$P17/logs/N_${id}_after.log" | tail -1 | grep -oE '^[0-9]+')
  nfail=${nfail:-0}
  if [ "$rc" -ne 0 ] && [ "$nfail" -gt 0 ]; then
    echo "  CRITERION          FAILED as expected ($nfail of 34 tests)"
    echo "  DETECTION          DETECTED, by:"
    grep -aE '\(Failed\)' "$P17/logs/N_${id}_after.log" \
      | sed -E 's/.*[0-9]+ - //; s/ \(Failed\)//' | sort -u | sed 's/^/      /'
  else
    echo "  CRITERION          *** DID NOT FAIL -- the criterion is VACUOUS ***"
    echo "  (suspect the PROBE SET first, then whether a defect was actually planted)"
  fi
  restore
  echo ""
}

log=$P17/logs/05_controls.log
{
echo "P17.5 GROUP N -- non-vacuity controls"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $STATE sha256: $STATE_SHA"
echo "baseline $EQN   sha256: $EQN_SHA"
echo "probe set: -R 'TotalEnergy'  = all 34 tests of the phase"
echo ""

# ---- N1: flip the sign of the DEFINING relation e = h - R T ----------------
sed -i 's|return enthalpy_.enthalpy(temperature) - thermo_.gasConstant() \* temperature;|return enthalpy_.enthalpy(temperature) + thermo_.gasConstant() * temperature;  // N1|' "$STATE"
run_control N1 "e = h + R T instead of h - R T (the defining relation, sign flipped)"

# ---- N2: drop the 0.5 from the kinetic energy ------------------------------
sed -i 's|return 0.5 \* dot(velocity, velocity);|return dot(velocity, velocity);  // N2|' "$STATE"
run_control N2 "ke = |U|^2, dropping the factor 0.5"

# ---- N3: drop the pressure work from the convective flux ------------------
# specificTotalEnthalpyField is what carries pressure work into the flux, because
# rho hTotal = rhoE + p. Returning eTotal instead removes exactly the p U term.
sed -i 's|result\[i\] = state.specificTotalEnthalpy(temperature\[i\], velocity\[i\]);|result[i] = state.specificTotalEnergy(temperature[i], velocity[i]);  // N3|' "$STATE"
run_control N3 "the energy flux carries eTotal instead of hTotal, i.e. NO pressure work"

# ---- N4: flip the sign of the conductive term -----------------------------
sed -i 's|const Real flux = terms.coefficient \* (temperature\[owner\] - temperature\[neighbour\]);|const Real flux = -terms.coefficient * (temperature[owner] - temperature[neighbour]);  // N4|' "$EQN"
run_control N4 "the conductive flux sign is flipped"

# ---- N5: transient uses the NEW density for both states -------------------
sed -i 's|result\[i\] = (rhoENew\[i\] - rhoEOld\[i\]) \* cell.volume() / timeStep;|result[i] = rhoENew[i] * 0.0 + (rhoENew[i] - rhoENew[i]) * cell.volume() / timeStep;  // N5|' "$EQN"
run_control N5 "the transient term differences rhoE_new against ITSELF (no old state)"

# ---- N6: rhoE omits the kinetic energy ------------------------------------
sed -i 's|return density \* specificTotalEnergy(temperature, velocity);|return density * specificInternalEnergy(temperature);  // N6|' "$STATE"
run_control N6 "rhoE = rho e, omitting the kinetic energy"

# ---- N7: accept an inadmissible internal energy instead of throwing -------
sed -i 's|if (!std::isfinite(out.specificInternalEnergy) \|\| out.specificInternalEnergy < eFloor) {|if (false) {  // N7|' "$STATE"
run_control N7 "an inadmissible recovered internal energy is ACCEPTED instead of throwing"

# ---- N8: h from cp*T instead of P15's closed-form integral ----------------
# Under the ZERO reference this is identical, so only the SHIFTED-reference tests can catch it --
# which is precisely why those tests exist.
python3 - "$STATE" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8", newline="").read()
old = "  return enthalpy_.enthalpy(temperature);\n}\n\nReal TotalEnergyState::specificInternalEnergy"
new = "  return thermo_.specificHeatPressure() * temperature;  // N8\n}\n\nReal TotalEnergyState::specificInternalEnergy"
assert old in s, "N8 anchor missing"
open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
PY
run_control N8 "h = cp*T instead of P15's closed-form enthalpy integral"

echo "=== restored, and proved back at baseline ==="
sha256sum "$STATE" "$EQN" | sed 's/^/  /'
echo "  expected: $STATE_SHA"
echo "            $EQN_SHA"
echo "  N-markers left: $(grep -c '// N[0-9]' "$STATE" "$EQN" | awk -F: '{s+=$2} END{print s}')  (0 required)"
cmake --build "$B" -j 16 > "$P17/logs/N_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'TotalEnergy' > "$P17/logs/N_restore.log" 2>&1
echo "  at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/N_restore.log" | tail -1)"
rm -f "$P17/controls"/*.pristine
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

#!/usr/bin/env bash
# P17.2 Group N -- non-vacuity controls. Each plants ONE defect, proves the intended
# criteria FAIL, then restores and verifies the restore by sha256.
#
# Gate: acceptance_gate.md section 10, sha256
# 1a0aefe24b7e41c2c302dfc25f67bf91faf6af9622dffb98a52be2d4dfb25c12
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/ideal_gas_eos
B=build/qual-final
EOSSRC=src/compressible/IdealGasEOS.cpp
THSRC=src/compressible/ThermodynamicProperties.cpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs" "$P17/controls"

cp "$EOSSRC" "$P17/controls/IdealGasEOS.cpp.pristine"
cp "$THSRC" "$P17/controls/ThermodynamicProperties.cpp.pristine"
EOS_SHA=$(sha256sum "$EOSSRC" | awk '{print $1}')
TH_SHA=$(sha256sum "$THSRC" | awk '{print $1}')

restore() {
  cp "$P17/controls/IdealGasEOS.cpp.pristine" "$EOSSRC"
  cp "$P17/controls/ThermodynamicProperties.cpp.pristine" "$THSRC"
  local a b
  a=$(sha256sum "$EOSSRC" | awk '{print $1}')
  b=$(sha256sum "$THSRC" | awk '{print $1}')
  if [ "$a" = "$EOS_SHA" ] && [ "$b" = "$TH_SHA" ]; then
    echo "  RESTORE            VERIFIED by SHA-256 (both files)"
  else
    echo "  RESTORE            *** FAILED ***"
  fi
}

# run_control <id> <description> <must-FAIL regex> <must-still-PASS regex or ->
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
    grep -aoE '[A-Za-z]+(Test)?\.[A-Za-z]+ .*Failed' "$P17/logs/N_${id}_after.log" \
      | grep -oE '^[A-Za-z]+(Test)?\.[A-Za-z]+' | sort -u | head -8 | sed 's/^/      /'
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
echo "P17.2 GROUP N -- non-vacuity controls"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $EOSSRC sha256: $EOS_SHA"
echo "baseline $THSRC  sha256: $TH_SHA"
echo ""

# ---- N1: drop R from temperature() -------------------------------------------
sed -i 's|const Real T = pressure / (density \* gasConstant_);|const Real T = pressure / density;  // N1|' "$EOSSRC"
run_control N1 "temperature() returns p/rho -- R dropped" \
  'IdealGasEOSTest.TemperatureMatchesPOverRhoR|IdealGasEOSTest.TemperatureToPressureRoundTrip|IdealGasEOSTest.ThreeWayClosure|IdealGasEOSTest.TemperatureIsReachable' \
  'IdealGasEOSTest.MatchesKnownStandardAtmosphereState|IdealGasEOSTest.PressureMatchesRhoRT'

# ---- N2: temperature() returns a constant ------------------------------------
sed -i 's|const Real T = pressure / (density \* gasConstant_);|const Real T = 288.15;  // N2|' "$EOSSRC"
run_control N2 "temperature() returns a constant" \
  'IdealGasEOSTest.TemperatureMatchesPOverRhoR|IdealGasEOSTest.TemperatureToPressureRoundTrip|IdealGasEOSTest.ThreeWayClosure|IdealGasEOSTest.TemperatureIsReachable' \
  '-'

# ---- N3: temperature() drops its INPUT validation ----------------------------
perl -0777 -pi -e 's/  requirePositiveFinite\(pressure, "pressure"\);\n  requirePositiveFinite\(density, "density"\);\n  const Real T = /  \/\/ N3\n  const Real T = /' "$EOSSRC"
run_control N3 "temperature() drops its input validation" \
  'IdealGasEOSTest.TemperatureRejectsNonPositivePressureOrDensity|IdealGasEOSTest.TemperatureRejectsNonFinitePressureOrDensity|IdealGasEOSTest.TemperatureMessageNames' \
  '-'

# ---- N4: perturb R by 1e-6, inside temperature() ONLY ------------------------
# THE SHARPEST EOS CONTROL: 1e-6 is six orders above the 1e-12 bound and invisible to a
# casual reader. C1/C2 must still pass, proving the NEW criteria detect it.
sed -i 's|const Real T = pressure / (density \* gasConstant_);|const Real T = pressure / (density * gasConstant_ * (1.0 + 1.0e-6));  // N4|' "$EOSSRC"
run_control N4 "perturb R by 1e-6 inside temperature() only" \
  'IdealGasEOSTest.TemperatureMatchesPOverRhoR|IdealGasEOSTest.TemperatureToPressureRoundTrip|IdealGasEOSTest.ThreeWayClosure|IdealGasEOSTest.TemperatureIsReachable' \
  'IdealGasEOSTest.MatchesKnownStandardAtmosphereState|IdealGasEOSTest.PressureMatchesRhoRT|IdealGasEOSTest.PressureToDensityRoundTripRecoversDensity'

# ---- N5: speedOfSound with gamma replaced by 1 -------------------------------
sed -i 's|return std::sqrt(specificHeatRatio_ \* gasConstant() \* temperature);|return std::sqrt(1.0 * gasConstant() * temperature);  // N5|' "$THSRC"
run_control N5 "speedOfSound uses gamma = 1" \
  'ThermodynamicPropertiesTest.SpeedOfSound|ThermodynamicPropertiesTest.SoundSpeedFrom' \
  '-'

# ---- N6: cv stored independently, rounded, instead of cp - R ------------------
# Attacks the exact T1/T2 asymmetry the dry-run exposed: perturbing cv breaks cp-cv=R at
# the 4 eps bound while gamma == cp/cv still holds, because gamma is STORED as that
# quotient of the perturbed cv.
sed -i 's|specificHeatVolume_ = specificHeatPressure_ - gasConstant;|specificHeatVolume_ = std::round((specificHeatPressure_ - gasConstant) * 1.0e10) / 1.0e10;  // N6|' "$THSRC"
run_control N6 "cv stored as a rounded independent value, not cp - R" \
  'ThermodynamicPropertiesTest.ClosureHoldsTogetherAcrossGases|ThermodynamicPropertiesTest.DerivesCvAndGamma' \
  'ThermodynamicPropertiesTest.GammaExceedsOneForEveryAdmissibleCp'

# ---- restored, and proved back at baseline ------------------------------------
echo "=== restored, and proved back at baseline ==="
sha256sum "$EOSSRC" "$THSRC" | sed 's/^/  /'
echo "  expected: $EOS_SHA"
echo "            $TH_SHA"
echo "  N-markers left: $(grep -c '// N[1-6]' "$EOSSRC" "$THSRC" | awk -F: '{s+=$2} END{print s}')  (0 required)"
cmake --build "$B" -j 16 > "$P17/logs/N_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'IdealGasEOSTest|ThermodynamicProperties|EvaluateDensityField|MachNumber' \
  > "$P17/logs/N_restore.log" 2>&1
echo "  EOS + thermo at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/N_restore.log" | tail -1)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

#!/usr/bin/env bash
# P17.2 control N6b -- the derived replacement for the VACUOUS N6.
# See AMENDMENT_N6_vacuous.md. delta = 1e-12, which is 2816x the minimum relative
# perturbation of cv that must exceed T1's 4*eps*R bound. Derived from the bound, not
# fitted to an observed result.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/ideal_gas_eos
B=build/qual-final
THSRC=src/compressible/ThermodynamicProperties.cpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"

cp "$THSRC" "$P17/controls/ThermodynamicProperties.cpp.pristine_n6b"
BASE=$(sha256sum "$THSRC" | awk '{print $1}')

log=$P17/logs/09_control_N6b.log
{
echo "P17.2 CONTROL N6b -- derived replacement for the vacuous N6"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $THSRC sha256: $BASE"
echo ""
echo "derivation (AMENDMENT_N6_vacuous.md section 3):"
echo "  T1 bound   = 4*eps*R = 2.5495161537492096e-13  absolute on (cp-cv)-R"
echo "  delta_min  = bound/cv = 3.5511054443195341e-16 = 1.60 eps"
echo "  delta used = 1e-12                             = 2816x delta_min"
echo "  predicted residual = -7.1810291046858765e-10   = 2816.6x the bound"
echo "  gamma - cp/cv must stay EXACTLY 0 (T2 survives, demonstrating the asymmetry)"
echo ""

sed -i 's|specificHeatVolume_ = specificHeatPressure_ - gasConstant;|specificHeatVolume_ = (specificHeatPressure_ - gasConstant) * (1.0 + 1.0e-12);  // N6b|' "$THSRC"
if ! grep -q 'N6b' "$THSRC"; then
  echo "  *** the sed did not apply -- control INVALID ***"
  echo "done: $(date -u +%FT%TZ)"; exit 1
fi
echo "  PLANTED DEFECT     $(grep -n 'N6b' "$THSRC" | sed 's/^/                     /')"

if ! cmake --build "$B" -j 16 > "$P17/logs/N6b_build.log" 2>&1; then
  echo "  did not compile -- control INVALID"
  grep -aE 'error:' "$P17/logs/N6b_build.log" | head -4 | sed 's/^/    /'
else
  echo "  build             exit 0"
  # must FAIL: T1 -- the cp-cv=R criterion
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
    -R 'ThermodynamicPropertiesTest.ClosureHoldsTogetherAcrossGases|ThermodynamicPropertiesTest.DerivesCvAndGamma' \
    --output-on-failure > "$P17/logs/N6b_after.log" 2>&1
  rc=$?
  nfail=$(grep -aoE '[0-9]+ tests failed' "$P17/logs/N6b_after.log" | tail -1 | grep -oE '^[0-9]+')
  nfail=${nfail:-0}
  if [ "$rc" -ne 0 ] && [ "$nfail" -gt 0 ]; then
    echo "  CRITERION T1      FAILED as expected ($nfail tests)"
    echo "  DETECTION         DETECTED -- T1 is PROVEN"
    grep -aoE '[A-Za-z]+Test\.[A-Za-z]+ .*Failed' "$P17/logs/N6b_after.log" \
      | grep -oE '^[A-Za-z]+Test\.[A-Za-z]+' | sort -u | sed 's/^/      /'
    echo "  the measured residual, from the failure output:"
    grep -aE 'Which is|Actual|absolute|error' "$P17/logs/N6b_after.log" | head -6 | sed 's/^/      /'
  else
    echo "  CRITERION T1      *** STILL DID NOT FAIL -- T1 REMAINS UNPROVEN, P17.2 BLOCKED ***"
  fi
  # must STILL PASS: T2/T3/T4 -- gamma == cp/cv, gamma > 1, cv > 0
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
    -R 'ThermodynamicPropertiesTest.GammaExceedsOneForEveryAdmissibleCp' \
    > "$P17/logs/N6b_stillpass.log" 2>&1
  echo "  MUST STILL PASS   GammaExceedsOneForEveryAdmissibleCp -> $(grep -aE 'tests passed|tests failed' "$P17/logs/N6b_stillpass.log" | tail -1)"
fi

cp "$P17/controls/ThermodynamicProperties.cpp.pristine_n6b" "$THSRC"
NOW=$(sha256sum "$THSRC" | awk '{print $1}')
if [ "$NOW" = "$BASE" ]; then echo "  RESTORE           VERIFIED by SHA-256"; else echo "  RESTORE           *** FAILED ***"; fi
echo "  N6b markers left: $(grep -c 'N6b' "$THSRC")  (0 required)"
cmake --build "$B" -j 16 > "$P17/logs/N6b_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'ThermodynamicProperties|IdealGasEOSTest|EvaluateDensityField|MachNumber' \
  > "$P17/logs/N6b_restore.log" 2>&1
echo "  at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/N6b_restore.log" | tail -1)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

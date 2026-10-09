#!/usr/bin/env bash
# P17.4 Group N -- non-vacuity controls.
#
# N4 as frozen ("momentum convection recomputes rho_f U_f from the cell density instead of
# using the passed massFlux") has NO code control here, and that is recorded rather than
# faked: the gate states F4 as a STRUCTURAL criterion, verified by reading the call site --
# CompressibleSIMPLE computes one massFlux and PASSES it to both the momentum assembly and
# the continuity evaluation. There is no second reconstruction to corrupt, so there is
# nothing to plant. Planting a scaled flux would test a different property (that the value
# matters) which N1/N2 already cover for the new term.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_momentum
B=build/qual-final
DIL=src/compressible/DilatationalStress.cpp
MOM=src/compressible/CompressibleMomentum.cpp
HDR=include/cfd/compressible/CompressibleRelaxedMomentum.hpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs" "$P17/controls"

for f in "$DIL" "$MOM" "$HDR"; do cp "$f" "$P17/controls/$(basename "$f").pristine"; done
DIL_SHA=$(sha256sum "$DIL" | awk '{print $1}')
MOM_SHA=$(sha256sum "$MOM" | awk '{print $1}')
HDR_SHA=$(sha256sum "$HDR" | awk '{print $1}')

restore() {
  cp "$P17/controls/$(basename "$DIL").pristine" "$DIL"
  cp "$P17/controls/$(basename "$MOM").pristine" "$MOM"
  cp "$P17/controls/$(basename "$HDR").pristine" "$HDR"
  local a b c
  a=$(sha256sum "$DIL" | awk '{print $1}')
  b=$(sha256sum "$MOM" | awk '{print $1}')
  c=$(sha256sum "$HDR" | awk '{print $1}')
  if [ "$a" = "$DIL_SHA" ] && [ "$b" = "$MOM_SHA" ] && [ "$c" = "$HDR_SHA" ]; then
    echo "  RESTORE            VERIFIED by SHA-256 (all three files)"
  else
    echo "  RESTORE            *** FAILED ***"
  fi
}

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
    grep -aoE '[A-Za-z]+Test\.[A-Za-z]+ .*Failed' "$P17/logs/N_${id}_after.log" \
      | grep -oE '^[A-Za-z]+Test\.[A-Za-z]+' | sort -u | head -8 | sed 's/^/      /'
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

log=$P17/logs/06_controls.log
{
echo "P17.4 GROUP N -- non-vacuity controls"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $DIL sha256: $DIL_SHA"
echo "baseline $MOM sha256: $MOM_SHA"
echo "baseline $HDR sha256: $HDR_SHA"
echo ""

# ---- N1: drop the -(2/3) factor -------------------------------------------
sed -i 's|constexpr Real kTwoThirds = 2.0 / 3.0;|constexpr Real kTwoThirds = 1.0;  // N1|' "$DIL"
run_control N1 "drop the -(2/3) factor (use -1)" \
  'DilatationalStressTest.MatchesMinusTwoThirdsMuGradDivergenceTimesVolume' \
  'DilatationalStressTest.IsExactlyZeroForADivergenceFreeField|DilatationalStressTest.UniformVelocityContributesNothing'

# ---- N2: drop the V_P factor ----------------------------------------------
sed -i 's|rhs\[cell.id()\] += -kTwoThirds \* dynamicViscosity \* gradComponent \* cell.volume();|rhs[cell.id()] += -kTwoThirds * dynamicViscosity * gradComponent;  // N2|' "$DIL"
run_control N2 "drop the V_P factor" \
  'DilatationalStressTest.MatchesMinusTwoThirdsMuGradDivergenceTimesVolume' \
  'DilatationalStressTest.IsExactlyZeroForADivergenceFreeField'

# ---- N3: use div U instead of grad(div U) ---------------------------------
# THE SHARPEST: proves it is the GRADIENT of the divergence that matters, not the
# divergence itself. Must break the analytical value AND the divergence-free property
# (solid-body rotation has div U = 0 so both stay zero -- but U = (x^2/2,0) has div U = x,
# non-zero, so the value check breaks).
sed -i 's|cfd::physics::velocityComponentValue(gradientOfDivergence\[cell.id()\], component);|divergenceU[cell.id()];  // N3|' "$DIL"
run_control N3 "use div U instead of grad(div U)" \
  'DilatationalStressTest.MatchesMinusTwoThirdsMuGradDivergenceTimesVolume|DilatationalStressTest.ScalesLinearly' \
  'DilatationalStressTest.IsExactlyZeroForADivergenceFreeField'

# ---- N5: flip the sign of the transient source ----------------------------
sed -i 's|result.source\[id\] = densityOld\[id\] \* volumeOverDt \* velocityOldComponent\[id\];|result.source[id] = -densityOld[id] * volumeOverDt * velocityOldComponent[id];  // N5|' "$MOM"
run_control N5 "flip the sign of the transient source term" \
  'CompressibleMomentumTimeDerivativeTest|CompressibleMomentumTermsTest.TransientTerm' \
  '-'

# ---- N6: make the dilatational option DEFAULT ENABLED ----------------------
# Proves default-off is LOAD-BEARING rather than decorative: it must disturb the validated
# compressible MMS, whose forcing is hand-derived for mu lap U only.
sed -i 's|bool includeDilatationalStress = false);|bool includeDilatationalStress = true);  // N6|' "$HDR"
run_control N6 "make the dilatational option default ENABLED" \
  'CompressibleSimpleMMS|LowMach|CompressibleSimpleTest' \
  '-'

echo "=== restored, and proved back at baseline ==="
sha256sum "$DIL" "$MOM" "$HDR" | sed 's/^/  /'
echo "  expected: $DIL_SHA"
echo "            $MOM_SHA"
echo "            $HDR_SHA"
echo "  N-markers left: $(grep -c '// N[1-6]' "$DIL" "$MOM" "$HDR" | awk -F: '{s+=$2} END{print s}')  (0 required)"
cmake --build "$B" -j 16 > "$P17/logs/N_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'CompressibleMomentum|DilatationalStress' \
  > "$P17/logs/N_restore.log" 2>&1
echo "  at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/N_restore.log" | tail -1)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

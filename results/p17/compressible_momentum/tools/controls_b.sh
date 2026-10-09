#!/usr/bin/env bash
# P17.4 Group N, SECOND PASS -- after the N2/N6 fixes (AMENDMENT_N2_N6_vacuous.md).
#
# N1 and N3 are re-run, not merely N2 and N6: the N2 fix changed the mesh of
# MatchesMinusTwoThirdsMuGradDivergenceTimesVolume, which is the SAME test N1 and N3 rely on
# for detection. A fix that repaired one control while silently disarming two others would be
# worse than the original defect, so all three are re-measured. N5 is re-run too -- its
# detectors are untouched, so it is a control ON THE CONTROL HARNESS: if N5's verdict moved,
# the harness itself changed and none of the other verdicts could be trusted.
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

for f in "$DIL" "$MOM" "$HDR"; do cp "$f" "$P17/controls/$(basename "$f").pristine2"; done
DIL_SHA=$(sha256sum "$DIL" | awk '{print $1}')
MOM_SHA=$(sha256sum "$MOM" | awk '{print $1}')
HDR_SHA=$(sha256sum "$HDR" | awk '{print $1}')

restore() {
  cp "$P17/controls/$(basename "$DIL").pristine2" "$DIL"
  cp "$P17/controls/$(basename "$MOM").pristine2" "$MOM"
  cp "$P17/controls/$(basename "$HDR").pristine2" "$HDR"
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
  if ! cmake --build "$B" -j 16 > "$P17/logs/Nb_${id}_build.log" 2>&1; then
    echo "  PLANTED DEFECT     did not compile -- control INVALID"
    grep -aE 'error:' "$P17/logs/Nb_${id}_build.log" | head -4 | sed 's/^/    /'
    restore; echo ""; return
  fi
  echo "  PLANTED DEFECT     yes (compiled)"
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$failRx" --output-on-failure \
    > "$P17/logs/Nb_${id}_after.log" 2>&1
  local rc=$? nfail
  nfail=$(grep -aoE '[0-9]+ tests failed' "$P17/logs/Nb_${id}_after.log" | tail -1 | grep -oE '^[0-9]+')
  nfail=${nfail:-0}
  if [ "$rc" -ne 0 ] && [ "$nfail" -gt 0 ]; then
    echo "  CRITERION          FAILED as expected ($nfail tests)"
    echo "  DETECTION          DETECTED"
  else
    echo "  CRITERION          *** DID NOT FAIL -- the criterion is STILL VACUOUS ***"
  fi
  # every row in the probed set, named, so a reader can see WHICH test did the detecting and
  # which stayed silent. A bare count cannot distinguish "the right test fired" from
  # "some other test happened to fail".
  echo "  per-test rows:"
  grep -aoE '[A-Za-z]+Test\.[A-Za-z]+ [.]* *(Passed|Failed)' "$P17/logs/Nb_${id}_after.log" \
    | sed -E 's/\.{2,}/ /' | sort -u | sed 's/^/      /'
  if [ "$passRx" != "-" ]; then
    OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$passRx" > "$P17/logs/Nb_${id}_stillpass.log" 2>&1
    printf '  MUST STILL PASS    -> %s\n' \
      "$(grep -aE 'tests passed|tests failed' "$P17/logs/Nb_${id}_stillpass.log" | tail -1)"
  fi
  restore
  echo ""
}

log=$P17/logs/08_controls_N2_N6b.log
{
echo "P17.4 GROUP N -- SECOND PASS (after the N2/N6 fixes)"
echo "date: $(date -u +%FT%TZ)"
echo "baseline $DIL sha256: $DIL_SHA"
echo "baseline $MOM sha256: $MOM_SHA"
echo "baseline $HDR sha256: $HDR_SHA"
echo "test file TESTs: $(grep -c '^TEST(' tests/unit/compressible/test_compressible_momentum.cpp)"
echo ""

# ---- N1 (re-run): drop the -(2/3) factor ----------------------------------
sed -i 's|constexpr Real kTwoThirds = 2.0 / 3.0;|constexpr Real kTwoThirds = 1.0;  // N1|' "$DIL"
run_control N1 "RE-RUN: drop the -(2/3) factor, on the NEW 20x20 mesh" \
  'DilatationalStressTest.MatchesMinusTwoThirdsMuGradDivergenceTimesVolume' \
  'DilatationalStressTest.IsExactlyZeroForADivergenceFreeField|DilatationalStressTest.UniformVelocityContributesNothing'

# ---- N2b: drop the V_P factor, now that V = 4 -----------------------------
sed -i 's|rhs\[cell.id()\] += -kTwoThirds \* dynamicViscosity \* gradComponent \* cell.volume();|rhs[cell.id()] += -kTwoThirds * dynamicViscosity * gradComponent;  // N2b|' "$DIL"
run_control N2b "drop the V_P factor (V = 4 now, so this is a 4x error)" \
  'DilatationalStressTest.MatchesMinusTwoThirdsMuGradDivergenceTimesVolume' \
  'DilatationalStressTest.IsExactlyZeroForADivergenceFreeField'

# ---- N3 (re-run): use div U instead of grad(div U) ------------------------
sed -i 's|cfd::physics::velocityComponentValue(gradientOfDivergence\[cell.id()\], component);|divergenceU[cell.id()];  // N3|' "$DIL"
run_control N3 "RE-RUN: use div U instead of grad(div U), on the NEW mesh" \
  'DilatationalStressTest.MatchesMinusTwoThirdsMuGradDivergenceTimesVolume|DilatationalStressTest.ScalesLinearly' \
  'DilatationalStressTest.IsExactlyZeroForADivergenceFreeField'

# ---- N5 (re-run): harness control -----------------------------------------
sed -i 's|result.source\[id\] = densityOld\[id\] \* volumeOverDt \* velocityOldComponent\[id\];|result.source[id] = -densityOld[id] * volumeOverDt * velocityOldComponent[id];  // N5|' "$MOM"
run_control N5 "RE-RUN (harness control): flip the transient source sign" \
  'CompressibleMomentumTimeDerivativeTest|CompressibleMomentumTermsTest.TransientTerm' \
  '-'

# ---- N6b: default the option to ENABLED ----------------------------------
# The FIRST N6 was vacuous because no test observed the DEFAULT: every test passed the flag
# explicitly, so flipping the default changed nothing. DefaultIsDisabledBehaviourally is the
# test that observes it. The per-test rows below are the proof of that diagnosis: if
# IsWiredIntoTheRelaxedMomentumAssembly passes here while DefaultIsDisabledBehaviourally
# fails, then the D3 wiring test ALONE would not have detected this, exactly as claimed.
sed -i 's|bool includeDilatationalStress = false);|bool includeDilatationalStress = true);  // N6b|' "$HDR"
run_control N6b "default the option to ENABLED" \
  'DilatationalStress|CompressibleMomentum' \
  'CompressibleSimpleMMS|LowMach'

echo "=== restored, and proved back at baseline ==="
sha256sum "$DIL" "$MOM" "$HDR" | sed 's/^/  /'
echo "  expected: $DIL_SHA"
echo "            $MOM_SHA"
echo "            $HDR_SHA"
echo "  N-markers left: $(grep -c '// N[0-9]b\?' "$DIL" "$MOM" "$HDR" | awk -F: '{s+=$2} END{print s}')  (0 required)"
cmake --build "$B" -j 16 > "$P17/logs/Nb_restore_build.log" 2>&1
echo "  rebuild exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'CompressibleMomentum|DilatationalStress' \
  > "$P17/logs/Nb_restore.log" 2>&1
echo "  at baseline: $(grep -aE 'tests passed|tests failed' "$P17/logs/Nb_restore.log" | tail -1)"
rm -f "$P17/controls"/*.pristine2
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

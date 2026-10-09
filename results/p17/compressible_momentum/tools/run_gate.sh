#!/usr/bin/env bash
# P17.4 acceptance gate, in the FROZEN order of acceptance_gate.md.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_momentum
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

log=$P17/logs/01_gate.log
{
echo "P17.4 ACCEPTANCE GATE"
echo "date: $(date -u +%FT%TZ)"
echo "gate sha256: $(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')"
echo ""

echo "### G6: clang-format on the files P17.4 changed ###"
clang-format-18 --dry-run --Werror \
  include/cfd/compressible/DilatationalStress.hpp \
  src/compressible/DilatationalStress.cpp \
  include/cfd/compressible/CompressibleRelaxedMomentum.hpp \
  src/compressible/CompressibleRelaxedMomentum.cpp \
  tests/unit/compressible/test_compressible_momentum.cpp 2>&1 | head -10
echo "  clang-format exit: ${PIPESTATUS[0]}  (0 = clean)"
echo ""

echo "### L5: physics::MomentumEquation must be UNTOUCHED ###"
# The probe here WAS `git diff --numstat HEAD`, and it reported this file TOUCHED although
# P17.4 never opened it: HEAD is 2026-09-21 and 286 files are uncommitted against it, so
# that probe measures every earlier uncommitted phase as well as this one. It was never
# dry-run at the baseline, where it would have printed the same non-empty delta before
# P17.4 wrote a single line. Replaced by the comparison the criterion actually states --
# against the hashes the freeze recorded for exactly this purpose. Non-vacuity is proven
# in logs/09_L5_instrument.log. See AMENDMENT_L5_instrument.md. The CRITERION is unchanged.
FROZEN_MOM_CPP=e46d49fcda78e0271538bc69117e249b0ea50dd2265c5f603d64d4206dc7bb59
FROZEN_MOM_HPP=7feca5c4a3a22943c442d1fa162b4e860ab037b69a3c6a6ce0e6c413bb0e37af
mom_cpp=$(sha256sum src/physics/MomentumEquation.cpp | awk '{print $1}')
mom_hpp=$(sha256sum include/cfd/physics/MomentumEquation.hpp | awk '{print $1}')
if [ "$mom_cpp" = "$FROZEN_MOM_CPP" ] && [ "$mom_hpp" = "$FROZEN_MOM_HPP" ]; then
  echo "  verdict: UNTOUCHED -- both files hash to the freeze"
else
  echo "  verdict: *** TOUCHED -- L5 VIOLATED ***"
  echo "    got      $mom_cpp  src/physics/MomentumEquation.cpp"
  echo "    expected $FROZEN_MOM_CPP"
  echo "    got      $mom_hpp  include/cfd/physics/MomentumEquation.hpp"
  echo "    expected $FROZEN_MOM_HPP"
fi
echo "  the DEFECTIVE original probe, on this same untouched tree, for the record:"
echo "    git diff --numstat HEAD -> '$(git diff --numstat HEAD -- src/physics/MomentumEquation.cpp include/cfd/physics/MomentumEquation.hpp | tr '\n' ' ')'"
echo ""

echo "### the P17.4 delta -- against the PRE-P17.4 sources, not against HEAD ###"
# The same defect L5 had: `git diff --numstat HEAD` charged P17.4 for earlier uncommitted
# phases. Of src/CMakeLists.txt's 32 added lines exactly ONE is P17.4's. The pre-P17.4 text
# of the two editable files is reconstructed and PROVEN against the frozen SHA-256 first;
# if that proof fails, the delta is not reported as fact.
python3 "$P17/tools/reconstruct_pre_p17_4.py"
PRE=$P17/controls/pre_p17_4
for f in include/cfd/compressible/CompressibleRelaxedMomentum.hpp \
         src/compressible/CompressibleRelaxedMomentum.cpp; do
  b=$PRE/$(basename "$f")
  if [ -f "$b" ]; then
    printf '  %-58s +%s -%s   (vs HEAD: %s)\n' "$f" \
      "$(diff "$b" "$f" | grep -c '^>')" "$(diff "$b" "$f" | grep -c '^<')" \
      "$(git diff --numstat HEAD -- "$f" | awk '{printf "+%s -%s", $1, $2}')"
  else
    printf '  %-58s RECONSTRUCTION FAILED -- delta NOT reported\n' "$f"
  fi
done
for f in include/cfd/compressible/DilatationalStress.hpp \
         src/compressible/DilatationalStress.cpp; do
  printf '  %-58s +%s -0   NEW FILE (untracked)\n' "$f" "$(wc -l < "$f")"
done
f=tests/unit/compressible/test_compressible_momentum.cpp
printf '  %-58s +%s -%s   (the HEAD copy has %s TEST( -- the freeze recorded 7, so clean)\n' "$f" \
  "$(git diff --numstat HEAD -- "$f" | awk '{print $1}')" \
  "$(git diff --numstat HEAD -- "$f" | awk '{print $2}')" \
  "$(git show HEAD:"$f" 2>/dev/null | grep -c '^TEST(')"
printf '  %-58s +%s -0   of which P17.4: %s  (the rest is P13-001/thermal/P16/GPU)\n' \
  src/CMakeLists.txt \
  "$(git diff --numstat HEAD -- src/CMakeLists.txt | awk '{print $1}')" \
  "$(git diff HEAD -- src/CMakeLists.txt | grep -c '^+.*DilatationalStress')"
echo ""

echo "### D1: the dilatational option must DEFAULT to disabled ###"
echo "  header default (source text): $(grep -c 'bool includeDilatationalStress = false' include/cfd/compressible/CompressibleRelaxedMomentum.hpp)  (1 = default false)"
echo "  BEHAVIOURAL probe            : DilatationalStressTest.DefaultIsDisabledBehaviourally"
echo "    A grep is a string in a header, not a property of the assembly: control N6"
echo "    flipped that default and NOTHING failed, because every test passed the flag"
echo "    explicitly. The named test calls the assembly with the argument OMITTED."
echo "    See AMENDMENT_N2_N6_vacuous.md section 2."
echo ""

echo "### build ###"
t0=$(date +%s)
cmake --build "$B" -j 16 > "$P17/logs/01_build.log" 2>&1
brc=$?
echo "  build exit: $brc  ($(( $(date +%s) - t0 )) s)"
if [ $brc -ne 0 ]; then
  echo "  *** BUILD FAILED -- stopping ***"
  grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/01_build.log" | head -20 | sed 's/^/    /'
  echo "done: $(date -u +%FT%TZ)"; exit 1
fi
echo ""

echo "### Groups D, F, V, G9: the momentum suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'CompressibleMomentum|DilatationalStress' --output-on-failure \
  > "$P17/logs/02_momentum.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/02_momentum.log" | tail -1 | sed 's/^/  /'
echo "  the 11 P17.4 rows (9 planned at the freeze + 2 added after the vacuous controls):"
for t in MatchesMinusTwoThirdsMuGradDivergenceTimesVolume IsExactlyZeroForADivergenceFreeField \
         UniformVelocityContributesNothing ScalesLinearlyWithViscosityAndVanishesAtZeroViscosity \
         RejectsInvalidInputs RepeatedAssemblyIsBitwiseIdentical \
         PressureForceMatchesMinusVolumeTimesAnalyticalGradient \
         BodyForceScalesLinearlyWithDensity TransientTermDistinguishesDRhoUDtFromRhoDUDt \
         IsWiredIntoTheRelaxedMomentumAssembly DefaultIsDisabledBehaviourally; do
  printf '    %-56s %s\n' "$t" \
    "$(grep -aoE "$t .*(Passed|Failed)" "$P17/logs/02_momentum.log" | grep -oE 'Passed|Failed' | head -1)"
done
echo ""

echo "### Group L: low-Mach + MMS, and the whole compressible suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'LowMach|MMSCompressible|CompressibleSimpleMMS' \
  --output-on-failure > "$P17/logs/03_low_mach.log" 2>&1
echo "  low-Mach + MMS exit: $?   $(grep -aE 'tests passed|tests failed' "$P17/logs/03_low_mach.log" | tail -1)"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible|Dilatational' --output-on-failure \
  > "$P17/logs/04_compressible_suite.log" 2>&1
echo "  compressible suite exit: $?   $(grep -aE 'tests passed|tests failed' "$P17/logs/04_compressible_suite.log" | tail -1)"
echo ""

echo "### Groups G1/G2: P14, P15, P16 preservation -- FROZEN narrow regexes ###"
for pair in "P14_transport:Transport|LegacyMatrix|ScalarTransport" \
            "P15_thermal:EnergyEquation|Enthalpy|ThermalPropert|TemperatureDependent|FixedTemperature|HeatFlux|ConvectiveBoundary|Boussinesq|TurbulentConductivity" \
            "P16_turbulence:Turbulence|EffectiveViscosity|WallFunction|WallTreatment|KEpsilon|KOmega"; do
  label=${pair%%:*}
  rx=${pair#*:}
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$rx" > "$P17/logs/05_${label}.log" 2>&1
  printf '  %-16s exit=%s  %s\n' "$label" "$?" \
    "$(grep -aE 'tests passed|tests failed' "$P17/logs/05_${label}.log" | tail -1)"
done
echo ""

echo "### G8: GPU compressible momentum -- must be NOT PRESENT ###"
for s in compressibleMomentum CompressibleRelaxed dilatational DilatationalStress; do
  printf '    %-24s %s\n' "$s" "$(grep -rliI -- "$s" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

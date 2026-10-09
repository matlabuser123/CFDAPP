#!/usr/bin/env bash
# P17.3 acceptance gate, in the FROZEN order of acceptance_gate.md.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_continuity
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

log=$P17/logs/01_gate.log
{
echo "P17.3 ACCEPTANCE GATE"
echo "date: $(date -u +%FT%TZ)"
echo "gate sha256: $(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')"
echo ""

echo "### G6: clang-format on the files P17.3 changed ###"
clang-format-18 --dry-run --Werror \
  include/cfd/compressible/CompressibleContinuity.hpp \
  src/compressible/CompressibleContinuity.cpp \
  tests/unit/compressible/test_compressible_continuity.cpp \
  tests/unit/compressible/test_compressible_mass_flux.cpp 2>&1 | head -10
echo "  clang-format exit: ${PIPESTATUS[0]}  (0 = clean)"
echo ""

echo "### physics::evaluateContinuity must be UNTOUCHED ###"
echo "  delta: '$(git diff --numstat HEAD -- src/physics/ContinuityEquation.cpp include/cfd/physics/ContinuityEquation.hpp | tr '\n' ' ')'  (empty = unchanged)"
echo ""

echo "### the P17.3 delta (deletions must be 0) ###"
for f in include/cfd/compressible/CompressibleContinuity.hpp \
         src/compressible/CompressibleContinuity.cpp \
         tests/unit/compressible/test_compressible_continuity.cpp \
         tests/unit/compressible/test_compressible_mass_flux.cpp; do
  printf '  %-58s %s\n' "$f" "$(git diff --numstat HEAD -- "$f" | awk '{printf "+%s -%s", $1, $2}')"
done
echo ""

echo "### build ###"
t0=$(date +%s)
cmake --build "$B" -j 16 > "$P17/logs/01_build.log" 2>&1
brc=$?
echo "  build exit: $brc  ($(( $(date +%s) - t0 )) s)"
if [ $brc -ne 0 ]; then
  echo "  *** BUILD FAILED -- stopping, per the gate stop rule ***"
  grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/01_build.log" | head -20 | sed 's/^/    /'
  echo "done: $(date -u +%FT%TZ)"
  exit 1
fi
echo ""

echo "### Groups S, T, F, C, M, O, V, G9: the continuity suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'CompressibleContinuityTest' --output-on-failure \
  > "$P17/logs/02_continuity.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/02_continuity.log" | tail -1 | sed 's/^/  /'
echo "  the 11 P17.3 rows:"
for t in GlobalNetFluxSumsBoundaryFacesOnly UniformDivergenceFreeFluxGivesExactlyZeroImbalance \
         InteriorFaceContributionsCancelGlobally ReducesToTheSteadyEvaluatorWhenDensityIsUnchanged \
         TransientIntegralMassConservationHolds RejectsNonFiniteOrNonPositiveDensity \
         RejectsNonFiniteMassFlux RejectionMessagesNameTheOffendingIndex \
         QuiescentClosedBoxConservesExactly TotalMassMatchesSumOfRhoTimesVolume \
         RepeatedEvaluationIsBitwiseIdentical; do
  printf '    %-56s %s\n' "$t" \
    "$(grep -aoE "$t .*(Passed|Failed)" "$P17/logs/02_continuity.log" | grep -oE 'Passed|Failed' | head -1)"
done
echo ""

echo "### Group M4: the rho*U = const refinement study ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'CompressibleMassFluxTest' --output-on-failure \
  > "$P17/logs/03_mass_flux.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/03_mass_flux.log" | tail -1 | sed 's/^/  /'
printf '    %-56s %s\n' "RhoTimesUConstantConvergesAtSecondOrder" \
  "$(grep -aoE 'RhoTimesUConstantConvergesAtSecondOrder .*(Passed|Failed)' "$P17/logs/03_mass_flux.log" | grep -oE 'Passed|Failed' | head -1)"
echo ""

echo "### Group L: low-Mach reduction, existing suite unchanged ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'LowMach|MMSCompressible|CompressibleSimpleMMS' \
  --output-on-failure > "$P17/logs/04_low_mach.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/04_low_mach.log" | tail -1 | sed 's/^/  /'
echo ""

echo "### Groups G1/G2: the whole compressible suite, then P14/P15/P16 ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible' --output-on-failure \
  > "$P17/logs/05_compressible_suite.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/05_compressible_suite.log" | tail -1 | sed 's/^/  /'
for pair in "P14_transport:Transport|LegacyMatrix|ScalarTransport" \
            "P15_thermal:EnergyEquation|Enthalpy|ThermalPropert|TemperatureDependent|FixedTemperature|HeatFlux|ConvectiveBoundary|Boussinesq|TurbulentConductivity" \
            "P16_turbulence:Turbulence|EffectiveViscosity|WallFunction|WallTreatment|KEpsilon|KOmega"; do
  label=${pair%%:*}
  rx=${pair#*:}
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$rx" > "$P17/logs/06_${label}.log" 2>&1
  printf '  %-16s exit=%s  %s\n' "$label" "$?" \
    "$(grep -aE 'tests passed|tests failed' "$P17/logs/06_${label}.log" | tail -1)"
done
echo ""

echo "### G8: GPU compressible continuity -- must be NOT PRESENT ###"
echo "  files in cuda/ or include/cfd/gpu/ mentioning 'compressible': $(grep -rliI compressible cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
grep -rliI compressible cuda/ include/cfd/gpu/ 2>/dev/null | sed 's/^/    /'
echo "  (the one hit is DevicePressureCorrection.hpp, recording that it ports the"
echo "   INCOMPRESSIBLE assembly -- there is no compressible continuity kernel)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

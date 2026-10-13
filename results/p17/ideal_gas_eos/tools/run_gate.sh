#!/usr/bin/env bash
# P17.2 acceptance gate, in the FROZEN order of acceptance_gate.md.
# Gate sha256: 1a0aefe24b7e41c2c302dfc25f67bf91faf6af9622dffb98a52be2d4dfb25c12
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/ideal_gas_eos
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

log=$P17/logs/01_gate.log
{
echo "P17.2 ACCEPTANCE GATE"
echo "date: $(date -u +%FT%TZ)"
echo "gate sha256: $(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')"
echo "  frozen was: 1a0aefe24b7e41c2c302dfc25f67bf91faf6af9622dffb98a52be2d4dfb25c12"
echo ""

echo "### G9: clang-format on the files P17.2 changed ###"
clang-format-18 --dry-run --Werror \
  include/cfd/compressible/EquationOfState.hpp \
  include/cfd/compressible/IdealGasEOS.hpp \
  src/compressible/IdealGasEOS.cpp \
  tests/unit/compressible/test_ideal_gas_eos.cpp \
  tests/unit/compressible/test_thermodynamic_properties.cpp 2>&1 | head -12
echo "  clang-format exit: ${PIPESTATUS[0]}  (0 = clean)"
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

echo "### Groups C, V, D: the EOS suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'IdealGasEOSTest' --output-on-failure \
  > "$P17/logs/02_eos.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/02_eos.log" | tail -1 | sed 's/^/  /'
echo "  the 9 P17.2 rows:"
for t in TemperatureMatchesPOverRhoR TemperatureToPressureRoundTripRecoversPressure \
         ThreeWayClosureHoldsAcrossGasesAndStateRanges TemperatureIsReachableThroughTheAbstraction \
         TemperatureRejectsNonPositivePressureOrDensity TemperatureRejectsNonFinitePressureOrDensity \
         TemperatureRejectsAnOverflowingQuotient TemperatureMessageNamesTheOffendingField \
         EveryDirectionIsBitwiseDeterministic; do
  printf '    %-52s %s\n' "$t" \
    "$(grep -aoE "$t .*(Passed|Failed)" "$P17/logs/02_eos.log" | grep -oE 'Passed|Failed' | head -1)"
done
echo ""

echo "### Groups T, A, I, D2: the thermodynamics suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'ThermodynamicProperties|MachNumber|EvaluateDensityField' --output-on-failure \
  > "$P17/logs/03_closure.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/03_closure.log" | tail -1 | sed 's/^/  /'
echo "  the 5 P17.2 rows:"
for t in ClosureHoldsTogetherAcrossGases GammaExceedsOneForEveryAdmissibleCp \
         SoundSpeedFromTemperatureMatchesSoundSpeedFromPressureAndDensity \
         EosDerivedDensityReachesTheAuthoritativeFieldWithTheExpectedValue \
         RepeatedEvaluationIsBitwiseIdentical; do
  printf '    %-62s %s\n' "$t" \
    "$(grep -aoE "$t .*(Passed|Failed)" "$P17/logs/03_closure.log" | grep -oE 'Passed|Failed' | head -1)"
done
echo ""

echo "### Group K: P15 constant-property compatibility -- EXISTING tests, not duplicated ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'SensibleEnthalpy|TemperatureProperty' \
  --output-on-failure > "$P17/logs/04_p15_constant_property.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/04_p15_constant_property.log" | tail -1 | sed 's/^/  /'
for t in ConstantCpGivesTheLinearForm RoundTripHoldsForConstantCpToo \
         OutOfRangeEnthalpyIsRefusedNotClamped ConstantIsTheSingleCoefficientCase; do
  printf '    %-46s %s\n' "$t" \
    "$(grep -aoE "$t .*(Passed|Failed)" "$P17/logs/04_p15_constant_property.log" | grep -oE 'Passed|Failed' | head -1)"
done
echo "  K4: e(T) must remain ABSENT -- internalEnergy hits: $(grep -rniI 'internalEnergy' include src | wc -l)  (must be 0)"
echo "  K3: P15 files untouched by P17.2:"
for f in include/cfd/thermal/Enthalpy.hpp include/cfd/thermal/TemperatureDependentProperty.hpp; do
  printf '    %-56s %s\n' "$f" "$(git diff --numstat HEAD -- "$f" | awk '{printf "+%s -%s", $1, $2}' || echo 'unchanged vs HEAD')"
done
echo ""

echo "### Group I3: no duplicated EOS arithmetic on the production path ###"
n=$(grep -rnE '/ *\( *[a-zA-Z_]*[Rr][a-zA-Z_]* *\* *[a-zA-Z_]*[Tt]|gasConstant\(\) *\* *|\* *gasConstant\(\)' \
      src/ include/ --include=*.cpp --include=*.hpp | grep -vc 'IdealGasEOS\|ThermodynamicProperties')
echo "  p/(R*T)-style arithmetic outside the EOS classes: $n   (must be 0)"
echo ""

echo "### Groups G1/G2: the whole compressible suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible' --output-on-failure \
  > "$P17/logs/05_compressible_suite.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/05_compressible_suite.log" | tail -1 | sed 's/^/  /'
echo ""

echo "### Groups G3/G4/G5: P14, P15, P16 preservation -- FROZEN narrow regexes ###"
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
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

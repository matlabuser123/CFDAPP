#!/usr/bin/env bash
# P17.1 acceptance gate, run in the FROZEN order of acceptance_gate.md.
# Gate sha256: 876466e2c97fd8db93e1dd96ac834a18a0c1c99ee8d381ab45b324b6dedc39ff
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/variable_density
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

log=$P17/logs/01_gate.log
{
echo "P17.1 ACCEPTANCE GATE"
echo "date: $(date -u +%FT%TZ)"
echo "gate sha256: $(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')"
echo "  frozen was: 876466e2c97fd8db93e1dd96ac834a18a0c1c99ee8d381ab45b324b6dedc39ff"
echo ""

echo "### build ###"
t0=$(date +%s)
cmake --build "$B" -j 16 > "$P17/logs/01_build.log" 2>&1
brc=$?
echo "  build exit: $brc  ($(( $(date +%s) - t0 )) s)"
if [ $brc -ne 0 ]; then
  echo "  *** BUILD FAILED -- stopping, per the gate stop rule ***"
  grep -aE '(^|[^a-zA-Z])error:|FAILED' "$P17/logs/01_build.log" | head -20 | sed 's/^/    /'
  echo "done: $(date -u +%FT%TZ)"
  exit 1
fi
echo ""

echo "### Groups D, S, M: the new and existing mass-flux criteria ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -R 'CompressibleMassFluxTest' --output-on-failure \
  > "$P17/logs/02_mass_flux.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/02_mass_flux.log" | tail -1 | sed 's/^/  /'
echo "  the 8 P17.1 rows:"
for t in RejectsNegativeCellDensity RejectsZeroCellDensity RejectsNonFiniteCellDensity \
         FaceDensityEvaluationRejectsInvalidCellDensity \
         ValidUniformDensityIsUnaffectedByTheNewValidation \
         ReversingVelocityReversesMassFluxExactly FaceDensityIsRecomputedPerCallNotCached \
         RowVaryingDensityConservesMassAndScalesFluxPerRow; do
  printf '    %-52s %s\n' "$t" \
    "$(grep -aoE "$t .*(Passed|Failed)" "$P17/logs/02_mass_flux.log" | grep -oE 'Passed|Failed' | head -1)"
done
echo ""

echo "### Group T: thermodynamic closure -- p/T coupling (existing EOS suite) ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -R 'IdealGasEOSTest|ThermodynamicProperties' \
  --output-on-failure > "$P17/logs/03_closure.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/03_closure.log" | tail -1 | sed 's/^/  /'
echo "  the coupling rows:"
for t in DoublingPressureAtFixedTemperatureDoublesDensity \
         DoublingTemperatureAtFixedPressureHalvesDensity \
         DDensityDPressureMatchesAnalyticalFormula DDensityDTemperatureIsNegative; do
  printf '    %-52s %s\n' "$t" \
    "$(grep -aoE "$t .*(Passed|Failed)" "$P17/logs/03_closure.log" | grep -oE 'Passed|Failed' | head -1)"
done
echo ""

echo "### Group G3: the boundary-EOS instrument must still REACH the boundary EOS ###"
printf '    %-52s %s\n' "ThrowsOnNonPositiveBoundaryTemperature" \
  "$(grep -aoE 'ThrowsOnNonPositiveBoundaryTemperature .*(Passed|Failed)' "$P17/logs/02_mass_flux.log" | grep -oE 'Passed|Failed' | head -1)"
echo "    (it sets temperature[0] = -1.0 with a VALID density, so the up-front density"
echo "     check cannot intercept it -- it still reaches the boundary EOS by construction)"
echo ""

echo "### Group C2: incompressible SIMPLE must remain EOS-free (structural) ###"
n=$(grep -rn 'EquationOfState\|IdealGas\|ThermodynamicProperties' \
      src/pressure_velocity/ include/cfd/pressure_velocity/ 2>/dev/null | wc -l)
echo "  EOS references under src/ and include/cfd/pressure_velocity: $n   (must be 0)"
echo ""

echo "### Group G1/G2: the whole existing compressible suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -R 'Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible' \
  --output-on-failure > "$P17/logs/04_compressible_suite.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/04_compressible_suite.log" | tail -1 | sed 's/^/  /'
echo ""

echo "### Groups G4/G5/G6: P14, P15, P16 preservation -- the FROZEN narrow regexes ###"
for pair in "P14_transport:Transport|LegacyMatrix|ScalarTransport" \
            "P15_thermal:EnergyEquation|Enthalpy|ThermalPropert|TemperatureDependent|FixedTemperature|HeatFlux|ConvectiveBoundary|Boussinesq|TurbulentConductivity" \
            "P16_turbulence:Turbulence|EffectiveViscosity|WallFunction|WallTreatment|KEpsilon|KOmega"; do
  label=${pair%%:*}
  rx=${pair#*:}
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -R "$rx" > "$P17/logs/05_${label}.log" 2>&1
  printf '  %-16s exit=%s  %s\n' "$label" "$?" \
    "$(grep -aE 'tests passed|tests failed' "$P17/logs/05_${label}.log" | tail -1)"
done
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

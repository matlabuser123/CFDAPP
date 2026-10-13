#!/usr/bin/env bash
# P17.2 acceptance gate: build, the new tests, then the preservation set.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible/eos
B=build/qual-final
cd "$ROOT"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

log=$P17/logs/01_gate.log
mkdir -p "$P17/logs"
{
echo "P17.2 gate"
echo "date: $(date -u +%FT%TZ)"
echo "tree: $B"
echo ""

echo "### build ###"
cmake --build "$B" -j 16 > "$P17/logs/01_build.log" 2>&1
rc=$?
echo "  build exit: $rc"
grep -aE '(^|[^a-zA-Z])error:|FAILED' "$P17/logs/01_build.log" | head -12 | sed 's/^/    /'
[ $rc -ne 0 ] && { echo "  *** BUILD FAILED -- stopping ***"; exit 1; }
echo ""

echo "### R, V: the new EOS tests ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -R 'IdealGasEOSTest' --output-on-failure \
  > "$P17/logs/02_eos_tests.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed out of' "$P17/logs/02_eos_tests.log" | tail -1 | sed 's/^/  /'
echo "  the P17.2 rows:"
grep -aoE 'Test +#[0-9]+: IdealGasEOSTest\.(Pressure[A-Za-z]*) \.+ +[A-Za-z]+' \
  "$P17/logs/02_eos_tests.log" | sed -E 's/Test +#[0-9]+: //' | sed 's/^/    /'
echo ""

echo "### T: the thermodynamic invariants ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -R 'ThermodynamicProperties' --output-on-failure \
  > "$P17/logs/03_thermo.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed out of' "$P17/logs/03_thermo.log" | tail -1 | sed 's/^/  /'
echo ""

echo "### G2: the whole existing compressible suite (90 tests / 13 files) ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -R 'Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible' \
  --timeout 7200 --output-on-failure > "$P17/logs/04_compressible_suite.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed out of' "$P17/logs/04_compressible_suite.log" | tail -1 | sed 's/^/  /'
grep -aE '\(Failed\)|\(Timeout\)' "$P17/logs/04_compressible_suite.log" | sed 's/^/    /'
echo ""

echo "### G4/G5/G6: P14, P15, P16 preservation ###"
for pair in "P14_transport:Transport|LegacyMatrix|ScalarTransport" \
            "P15_thermal:EnergyEquation|Enthalpy|ThermalPropert|TemperatureDependent|FixedTemperature|HeatFlux|ConvectiveBoundary|Boussinesq|TurbulentConductivity" \
            "P16_turbulence:Turbulence|EffectiveViscosity|WallFunction|WallTreatment|KEpsilon|KOmega"; do
  label=${pair%%:*}; rx=${pair#*:}
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -R "$rx" \
    -E 'SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic' \
    --timeout 7200 > "$P17/logs/05_$label.log" 2>&1
  printf '  %-16s exit=%s  %s\n' "$label" "$?" \
    "$(grep -aE 'tests passed|tests failed out of' "$P17/logs/05_$label.log" | tail -1)"
done
echo ""

echo "### G7: the P13-001 SIMPLE fingerprint must be BITWISE identical ###"
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include results/p13/restart/tools/solve_fingerprint.cpp \
    -o /tmp/p17_fp -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/06_fp_build.log"
if [ $? -ne 0 ]; then
  echo "  COMPILE FAILED"; tail -6 "$P17/logs/06_fp_build.log" | sed 's/^/    /'
else
  /tmp/p17_fp > "$P17/logs/06_fingerprint.log" 2>&1
  echo "  cases: $(grep -c FINGERPRINT "$P17/logs/06_fingerprint.log")"
  strip() { grep -v '^#' "$1" | grep -v '^$'; }
  if diff <(strip results/p13/restart/logs/fingerprint-after-histories.log) \
          <(strip "$P17/logs/06_fingerprint.log") > "$P17/logs/06_fp_diff.txt" 2>&1; then
    echo "  BITWISE IDENTICAL -- the EOS change moved no bit of the incompressible solver"
  else
    echo "  *** DIFFERS ***"; head -16 "$P17/logs/06_fp_diff.txt" | sed 's/^/    /'
  fi
fi
echo ""

echo "### the authorized source delta ###"
bash results/p13/final-qualification/2026-09-24/g4-cli-gui/tools/fingerprint_v2.sh P17_2_after \
  | grep -E 'SOURCE_FINGERPRINT_V2 =|tracked engineering|untracked engineering' | sed 's/^/  /'
echo "  P13 final / P17 baseline: 721ce161aafbf6958a4620c4d9fe4b4f332dbba3519f6e9b9496548213901955"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

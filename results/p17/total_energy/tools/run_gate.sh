#!/usr/bin/env bash
# P17.5 acceptance gate, in the frozen order of acceptance_gate.md.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/total_energy
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

FILES="include/cfd/compressible/TotalEnergy.hpp
src/compressible/TotalEnergy.cpp
include/cfd/compressible/TotalEnergyEquation.hpp
src/compressible/TotalEnergyEquation.cpp
tests/unit/compressible/test_total_energy.cpp"

log=$P17/logs/03_gate.log
{
echo "P17.5 ACCEPTANCE GATE"
echo "date: $(date -u +%FT%TZ)"
echo "gate sha256: $(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')"
echo "TESTs in test_total_energy.cpp: $(grep -c '^TEST(' tests/unit/compressible/test_total_energy.cpp)"
echo ""

echo "### P7: clang-format on every changed file ###"
# shellcheck disable=SC2086
clang-format-18 -i $FILES
# shellcheck disable=SC2086
clang-format-18 --dry-run --Werror $FILES 2>&1 | head -6
echo "  clang-format exit after -i: ${PIPESTATUS[0]}  (0 = clean)"
echo ""

echo "### P8/P9: upstream files must be UNTOUCHED -- SHA-256, never git diff HEAD ###"
# P17.4's L5 used `git diff --numstat HEAD` and reported a file TOUCHED that the phase never
# opened, because nothing since P12 is committed. That probe is banned by this gate.
untouched=0
touched=0
check() {  # check <expected sha256> <path>
  local got
  got=$(sha256sum "$2" | awk '{print $1}')
  if [ "$got" = "$1" ]; then
    untouched=$((untouched + 1))
  else
    touched=$((touched + 1))
    echo "  *** TOUCHED: $2"
    echo "        got      $got"
    echo "        expected $1"
  fi
}
check 6fa846217f09cafb982cbe01690e8e0aa6a61f35105babc3894f4d6e0ac5876e include/cfd/thermal/EnergyEquation.hpp
check 910214817717b6e8f95ff7c0f8c0294c875b9c316244320a4cdb04a24db2b3e5 src/thermal/EnergyEquation.cpp
check 9e7e758fba65f7c22e91f1624df8d4d71f8c4d818da4a295f5e43a0485fb1dee src/thermal/Enthalpy.cpp
check f429b79bc9b8bf2c2ed711946f317ea58730beb6cc6e1042a35d2bb37b4c19bd include/cfd/thermal/ThermalProperties.hpp
check e72f53ca3c83324377d94a8b67997f416e26bc810d91e981dc4606581d98721e include/cfd/thermal/TemperatureDependentProperty.hpp
check bdd096490437ee602de31695184035e2ce28f0ab4d2da6187e45c709e919324e include/cfd/compressible/IdealGasEOS.hpp
check daa27c5fc7e296482fd29fd93a0b4b6185fc7f3d39bc72855aa6240aad3bfe24 src/compressible/IdealGasEOS.cpp
check 1dd39383f5d87eb77a5b69252737db0cd0640121ea6e19eb5ec3916377008335 include/cfd/compressible/ThermodynamicProperties.hpp
check 27d74d9e535f8e716421ce24e7f955f32dc8200c5ae5a3ddfd1c9cbdb4da3a88 src/compressible/ThermodynamicProperties.cpp
check d376375ce2308b2ffe16968d2a063bda595d70194de9d6eb8faa147e1c1a0ea3 include/cfd/compressible/CompressibleMassFlux.hpp
check 2864c6ebb8b85b35ffd32c88544bb427fa3d87cd573569c60b6b53a808d95ca7 src/compressible/CompressibleMassFlux.cpp
check 842b927fb63f9cff4306fa8d1f1c8bbc3e06088dffd8e9b88308513e253dcc5b include/cfd/compressible/DilatationalStress.hpp
check fc00192ddb9d024fa1c61fcf6d7911fb7b862ec811ddc503b10e18370bc0c5e3 src/compressible/DilatationalStress.cpp
check 7feca5c4a3a22943c442d1fa162b4e860ab037b69a3c6a6ce0e6c413bb0e37af include/cfd/physics/MomentumEquation.hpp
check e46d49fcda78e0271538bc69117e249b0ea50dd2265c5f603d64d4206dc7bb59 src/physics/MomentumEquation.cpp
echo "  UNTOUCHED: $untouched    TOUCHED: $touched   (TOUCHED must be 0)"
echo ""

echo "### Z1: the energy path must never read the EOS block's cp ###"
echo "  specificHeatPressure() call sites in the new sources: $(grep -c 'specificHeatPressure' src/compressible/TotalEnergy.cpp)"
echo "    (the ONLY legitimate uses are the dual-cp consistency CHECK in the constructor and the"
echo "     closed-form inverse, which needs cp and cv together. Enthalpy itself always comes from"
echo "     the P15 model -- see TotalEnergyContractTest.EnthalpyComesFromTheModelNotTheEosSpecificHeat)"
echo ""

echo "### build ###"
t0=$(date +%s)
cmake --build "$B" -j 16 > "$P17/logs/03_build.log" 2>&1
brc=$?
echo "  build exit: $brc  ($(( $(date +%s) - t0 )) s)"
if [ $brc -ne 0 ]; then
  echo "  *** BUILD FAILED -- stopping ***"
  grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/03_build.log" | head -25 | sed 's/^/    /'
  echo "done: $(date -u +%FT%TZ)"; exit 1
fi
echo ""

echo "### Groups T, C, E, G, V, D, Z: the total-energy suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'TotalEnergy' --output-on-failure \
  > "$P17/logs/03_total_energy.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/03_total_energy.log" | tail -1 | sed 's/^/  /'
echo "  every row, by name:"
grep -aoE 'TotalEnergy[A-Za-z]*Test\.[A-Za-z]+ [.]* *(Passed|Failed)' "$P17/logs/03_total_energy.log" \
  | sed -E 's/\.{2,}/ /' | sort -u | sed 's/^/    /'
echo ""
if grep -aqE '\(Failed\)' "$P17/logs/03_total_energy.log"; then
  echo "  --- FAILURES, preserved ---"
  grep -aE '\(Failed\)' "$P17/logs/03_total_energy.log" | sed 's/^/    /'
  echo ""
fi

echo "### P1/P2/P3: preservation -- frozen narrow regexes ###"
for pair in "P15_thermal:EnergyEquation|Enthalpy|ThermalPropert|TemperatureDependent|FixedTemperature|HeatFlux|ConvectiveBoundary|Boussinesq|TurbulentConductivity" \
            "P14_transport:Transport|LegacyMatrix|ScalarTransport"; do
  label=${pair%%:*}
  rx=${pair#*:}
  # -E TotalEnergy matters: P17.5 test NAMES contain "Enthalpy", so without the exclusion they
  # match the FROZEN P15 regex and the preservation figure silently becomes 161 + mine = 171.
  # Attempt 1 printed exactly that, and the "1 failed" it reported was one of MY tests, not
  # P15's. A preservation probe that includes the phase's own new tests is not one.
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$rx" -E "TotalEnergy" \
    > "$P17/logs/03_${label}.log" 2>&1
  printf '  %-16s exit=%s  %s\n' "$label" "$?" \
    "$(grep -aE 'tests passed|tests failed' "$P17/logs/03_${label}.log" | tail -1)"
done
echo ""

echo "### P2: the compressible suite, deliberately INCLUDING the new tests ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible|Dilatational|TotalEnergy' \
  > "$P17/logs/03_compressible.log" 2>&1
echo "  compressible exit=$?   $(grep -aE 'tests passed|tests failed' "$P17/logs/03_compressible.log" | tail -1)"
echo "    176 at the P17.4 baseline, plus the P17.5 tests"
echo ""

echo "### X1: the GPU compressible/energy surface -- expected NOT PRESENT, never PASS ###"
for s in compressible IdealGas EquationOfState ThermodynamicProperties Enthalpy rhoE \
         totalEnergy TotalEnergy machNumber; do
  printf '    %-24s %s\n' "$s" "$(grep -rliI -- "$s" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo "    the only nonzero hit is a COMMENT about the INCOMPRESSIBLE path:"
grep -rniI 'compressible' cuda/ include/cfd/gpu/ 2>/dev/null | head -2 | sed 's/^/      /'
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

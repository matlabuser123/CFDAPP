#!/usr/bin/env bash
# P17.6 acceptance gate, in the frozen order of acceptance_gate.md.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/low_mach
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

log=$P17/logs/03_gate.log
{
echo "P17.6 ACCEPTANCE GATE"
echo "date: $(date -u +%FT%TZ)"
echo "gate sha256: $(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')"
echo ""

echo "### P7: clang-format on the one changed file ###"
clang-format-18 -i tests/integration/compressible/test_low_mach_family.cpp
clang-format-18 --dry-run --Werror tests/integration/compressible/test_low_mach_family.cpp 2>&1 | head -6
echo "  clang-format exit after -i: ${PIPESTATUS[0]}  (0 = clean)"
echo ""

echo "### P1/P2/P8: the files that must be UNTOUCHED -- SHA-256, never git diff HEAD ###"
untouched=0
touched=0
check() {
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
# G5 requires these two UNCHANGED
check 593165a1a2fff9ccbad8b3675ccbdff093bcf22c82b5daad90462fe228949acc tests/integration/compressible/test_low_mach_regression.cpp
check 9c3ad6502e65543422e95c21e99354380c2cffeedeafd2d8b4b9fa3c83793796 tests/integration/mms/test_mms_compressible.cpp
# the incompressible SIMPLE references and the solver itself
check c33143bfc886b0695e8a11b852f0035575f8f8389ee24e4d60a47888e863d674 tests/integration/poiseuille/test_poiseuille_validation.cpp
check 8a6061c30da34bff819beca7c563126e636b505dca4c074c85ee6503ccd949a0 tests/integration/cavity/test_cavity_ghia.cpp
check 34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1 src/pressure_velocity/SIMPLE.cpp
check 325a7f380be58144794538cd64d0fd4f1324a46b1bc9fa2727d5fa899e2eb9da include/cfd/pressure_velocity/SIMPLE.hpp
# P17.1 - P17.5 production
check f95f760bbe2abb284ec33161209bf33f25ec9a0fb4a02e170a2232bd17d6d8ed src/compressible/CompressibleSIMPLE.cpp
check 2864c6ebb8b85b35ffd32c88544bb427fa3d87cd573569c60b6b53a808d95ca7 src/compressible/CompressibleMassFlux.cpp
check 5396726d935905d450df2bdb01d215ed29136f23d2f0f78f6dae4563b0b1f267 src/compressible/CompressibleContinuity.cpp
check daa27c5fc7e296482fd29fd93a0b4b6185fc7f3d39bc72855aa6240aad3bfe24 src/compressible/IdealGasEOS.cpp
check 27d74d9e535f8e716421ce24e7f955f32dc8200c5ae5a3ddfd1c9cbdb4da3a88 src/compressible/ThermodynamicProperties.cpp
check fc00192ddb9d024fa1c61fcf6d7911fb7b862ec811ddc503b10e18370bc0c5e3 src/compressible/DilatationalStress.cpp
check 23bde46f7a94d7e24ed262ef3ae5f4cc5677bb9194e01399014b12d929df16c5 src/compressible/TotalEnergy.cpp
check 2df1c9f54d5186fb5f22cf1f74fbb3c3aaaaddbbebef929df1853441b2c1956e src/compressible/TotalEnergyEquation.cpp
check 0bdff82ec3e717bc8fc8aac01f5bf06914153039673bcf43cfcfe8902532007d src/compressible/CompressibleRelaxedMomentum.cpp
echo "  UNTOUCHED: $untouched    TOUCHED: $touched   (TOUCHED must be 0)"
echo "  NOTE: P17.6 is a VALIDATION phase -- it adds ONE test file and ONE CMake line, and changes"
echo "        no production source at all."
echo ""

echo "### build ###"
t0=$(date +%s)
cmake -S . -B "$B" > "$P17/logs/03_configure.log" 2>&1
cmake --build "$B" -j 16 > "$P17/logs/03_build.log" 2>&1
brc=$?
echo "  build exit: $brc  ($(( $(date +%s) - t0 )) s)"
if [ $brc -ne 0 ]; then
  echo "  *** BUILD FAILED -- stopping ***"
  grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/03_build.log" | head -20 | sed 's/^/    /'
  echo "done: $(date -u +%FT%TZ)"; exit 1
fi
echo ""

echo "### Groups M, A, E, C, D, F: the NEW low-Mach family ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'LowMachFamily' --output-on-failure \
  > "$P17/logs/03_family.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/03_family.log" | tail -1 | sed 's/^/  /'
grep -aoE 'LowMachFamilyTest\.[A-Za-z]+ [.]* *(Passed|Failed)' "$P17/logs/03_family.log" \
  | sed -E 's/\.{2,}/ /' | sort -u | sed 's/^/    /'
echo ""

echo "### P1: the EXISTING low-Mach test, UNCHANGED, re-run ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'LowMachRegression' --output-on-failure \
  > "$P17/logs/03_lowmach_existing.log" 2>&1
echo "  exit=$?   $(grep -aE 'tests passed|tests failed' "$P17/logs/03_lowmach_existing.log" | tail -1)"
echo ""

echo "### P2/P3: MMS, Poiseuille, cavity ###"
for pair in "mms_compressible:MMSCompressible|CompressibleSimpleMMS" \
            "poiseuille:Poiseuille" \
            "cavity:CavityGhia|Ghia"; do
  label=${pair%%:*}
  rx=${pair#*:}
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$rx" > "$P17/logs/03_${label}.log" 2>&1
  printf '  %-18s exit=%s  %s\n' "$label" "$?" \
    "$(grep -aE 'tests passed|tests failed' "$P17/logs/03_${label}.log" | tail -1)"
done
echo ""

echo "### P5: preservation -- frozen narrow regexes ###"
for pair in "P14_transport:Transport|LegacyMatrix|ScalarTransport" \
            "P15_thermal:EnergyEquation|Enthalpy|ThermalPropert|TemperatureDependent|FixedTemperature|HeatFlux|ConvectiveBoundary|Boussinesq|TurbulentConductivity"; do
  label=${pair%%:*}
  rx=${pair#*:}
  OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R "$rx" -E 'TotalEnergy|LowMach' \
    > "$P17/logs/03_${label}.log" 2>&1
  printf '  %-18s exit=%s  %s\n' "$label" "$?" \
    "$(grep -aE 'tests passed|tests failed' "$P17/logs/03_${label}.log" | tail -1)"
done
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible|Dilatational|TotalEnergy' \
  > "$P17/logs/03_compressible.log" 2>&1
echo "  compressible       exit=$?   $(grep -aE 'tests passed|tests failed' "$P17/logs/03_compressible.log" | tail -1)"
echo "    210 at the P17.5 baseline, plus P17.6's 15"
echo ""

echo "### X1: the GPU compressible surface -- expected NOT PRESENT, never PASS ###"
for s in compressible IdealGas EquationOfState ThermodynamicProperties Enthalpy rhoE \
         totalEnergy TotalEnergy machNumber CompressibleSIMPLE; do
  printf '    %-24s %s\n' "$s" "$(grep -rliI -- "$s" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo "    the only nonzero hit is a COMMENT about the INCOMPRESSIBLE path:"
grep -rniI 'compressible' cuda/ include/cfd/gpu/ 2>/dev/null | head -2 | sed 's/^/      /'
echo "    => no GPU compressible solver exists, so there is no GPU low-Mach behaviour to preserve."
echo "       Reported NOT PRESENT. P17.10's qualification is NOT claimed."
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

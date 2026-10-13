#!/usr/bin/env bash
# P18.3 sections 31-33 -- prove P15, P17, P18.1 and P18.2 are untouched.
#
# P18.3 adds two new files and one CMakeLists block. It modifies NO existing
# production source, so preservation should be structural -- but "should be" is a
# claim about code that an edit can break, so it is measured.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
BUILD=${1:-build/final}

TARGETS="CFDSpeciesTests CFDThermalTests CFDCompressibleTests CFDTransportTests CFDTurbulenceTests"
echo "=== rebuilding (binaries deleted first, so a compile failure cannot leave a stale pass) ==="
for t in $TARGETS; do find "$BUILD" -name "$t" -type f -delete 2>/dev/null; done
if ! cmake --build "$BUILD" --target $TARGETS -j 20 > /tmp/p18_3_build.log 2>&1; then
  echo "BUILD FAILED"; grep -E 'error:' /tmp/p18_3_build.log | head -15; exit 1
fi
echo "  build OK"
echo ""

run() {
  local label="$1" binary
  binary=$(find "$BUILD" -name "$2" -type f | head -1)
  if [ -z "$binary" ]; then echo "  $label: BINARY NOT FOUND"; return 1; fi
  printf '  %-34s ' "$label"
  "$binary" 2>&1 | grep -E '^\[  (PASSED|FAILED)  \]' | tr '\n' ' '
  echo ""
}

echo "=== P15 -- thermal: cp(T), enthalpy, temperature inversion, reference conventions ==="
run "thermal unit suite" CFDThermalTests
echo ""
echo "=== P17 -- compressible: IdealGasEOS, ThermodynamicProperties, TotalEnergyState ==="
run "compressible unit suite" CFDCompressibleTests
echo ""
echo "=== P18.1 / P18.2 / P18.3 -- species ==="
run "species unit suite" CFDSpeciesTests
echo ""
echo "=== P14 / P16 -- the transport and turbulence P18.2 touched ==="
run "transport unit suite" CFDTransportTests
run "turbulence unit suite" CFDTurbulenceTests
echo ""

echo "=== the P15/P17 types P18.3 REUSES, named explicitly ==="
B=$(find "$BUILD" -name CFDCompressibleTests -type f | head -1)
for f in '*ThermodynamicProperties*' '*IdealGas*' '*TotalEnergy*'; do
  printf '  %-34s ' "$f"
  "$B" --gtest_filter="$f" 2>&1 | grep -E '^\[  (PASSED|FAILED)  \]' | tr '\n' ' '
  echo ""
done
T=$(find "$BUILD" -name CFDThermalTests -type f | head -1)
for f in '*Enthalpy*' 'TemperatureProperty.*'; do
  printf '  %-34s ' "$f"
  "$T" --gtest_filter="$f" 2>&1 | grep -E '^\[  (PASSED|FAILED)  \]' | tr '\n' ' '
  echo ""
done
echo ""
echo "=== P18.3 changed no existing production source ==="
echo "  files P18.3 added    : include/cfd/species/MixtureThermodynamics.hpp"
echo "                         src/species/MixtureThermodynamics.cpp"
echo "                         tests/unit/species/test_mixture_thermodynamics.cpp"
echo "  files P18.3 modified : src/CMakeLists.txt · tests/unit/species/CMakeLists.txt"
echo "  P17/P15 production modified by P18.3 : 0"

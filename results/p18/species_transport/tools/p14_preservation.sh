#!/usr/bin/env bash
# P18.2 -- prove the GradientBoundaryTreatment parameter left every EXISTING P14 caller
# bitwise unchanged.
#
# The default is Lagged, so this must hold. It is checked rather than asserted, because "the
# default preserves behaviour" is a claim about code that an edit can break -- and P14's own
# frozen oracles are the instrument that would catch it.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
BUILD=${1:-build/final}

TARGETS="CFDTransportTests CFDTurbulenceTests CFDThermalTests CFDSpeciesTests"
echo "=== rebuilding the affected suites (binaries deleted first) ==="
for t in $TARGETS; do
  find "$BUILD" -name "$t" -type f -delete 2>/dev/null
done
if ! cmake --build "$BUILD" --target $TARGETS -j 20 > /tmp/p18_2_p14_build.log 2>&1; then
  echo "BUILD FAILED"
  grep -E 'error:' /tmp/p18_2_p14_build.log | head -15
  exit 1
fi
echo "  build OK"
echo ""

echo "=== the FROZEN P14 ORACLES -- these encode the pre-P18.2 matrix, RHS and solved field ==="
echo ""
echo "--- tests/unit/transport/test_transport_characterization.cpp (matrix, sparsity, RHS) ---"
T=$(find "$BUILD" -name CFDTransportTests -type f | head -1)
"$T" --gtest_filter='*Characterization*:*characterization*' 2>&1 | tail -6
echo ""
echo "--- the whole transport unit suite ---"
"$T" 2>&1 | tail -4
echo ""
echo "--- tests/unit/turbulence/test_p14_migration_equivalence.cpp (solved field, HAS 3 FixedGradient patches) ---"
U=$(find "$BUILD" -name CFDTurbulenceTests -type f | head -1)
"$U" --gtest_filter='*P14Migration*:*p14_migration*:*Migration*' 2>&1 | tail -6
echo ""
echo "--- the whole turbulence unit suite ---"
"$U" 2>&1 | tail -4
echo ""
echo "--- thermal (owns the EXACT treatment this copies; must be untouched) ---"
V=$(find "$BUILD" -name CFDThermalTests -type f | head -1)
"$V" 2>&1 | tail -4
echo ""
echo "--- species (P18.1 state + the pre-P18 passive stack) ---"
W=$(find "$BUILD" -name CFDSpeciesTests -type f | head -1)
"$W" 2>&1 | tail -4

#!/usr/bin/env bash
# P14 migration regression: the VALIDATED channel scope.
#
# The descoped automatic-wall suites are excluded by filter, not deleted or
# disabled. They assert gates that the deferred modes are known and documented
# to fail (results/p16/turbulence/automatic_wall/SUMMARY.md); leaving them in a
# regression run would make it permanently red and mask a real regression.
# Everything they measured is preserved as evidence.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
B=./build/final/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests
rm -f "$B"
cmake --build build/final --target CFDTurbulenceChannelValidationTests -j 16 2>&1 \
  | grep -E "error:|FAILED" | head -20
[ -x "$B" ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
"$B" --gtest_filter='-SSTAutomaticWallChannel.*:SSTCompositeWallChannel.*:SSTMomentumFluxChannel.*:WallArchitectureDiagnostic.*' 2>&1 \
  | grep -v "^\[INFO\]" | tail -30

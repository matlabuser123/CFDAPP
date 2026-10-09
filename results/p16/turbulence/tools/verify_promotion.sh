#!/usr/bin/env bash
# The mu_t promotion must be behaviour-preserving: it moved a function out of
# an anonymous namespace, it did not change the formula. These are the suites
# that would see a difference.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
cmake --build build/cuda --target CFDTurbulenceTests CFDTurbulenceChannelValidationTests -j 16 2>&1 \
  | grep -E "error|FAILED" | head -20
echo "=== turbulence unit tests ==="
./build/cuda/tests/unit/turbulence/CFDTurbulenceTests 2>&1 | tail -6
echo ""
echo "=== channel validation, P16 validated scope (P20 suites excluded by filter) ==="
./build/cuda/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests \
  --gtest_filter='-SSTAutomaticWallChannel.*:SSTCompositeWallChannel.*:SSTMomentumFluxChannel.*:WallArchitectureDiagnostic.*' \
  2>&1 | grep -v "^\[INFO\]" | tail -8

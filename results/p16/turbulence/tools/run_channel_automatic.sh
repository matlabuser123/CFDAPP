#!/usr/bin/env bash
# P16b section 9/10: the automatic treatment on the SAME channels, same meshes,
# same metrics, against the FROZEN SST gates.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
B=./build/final/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests
rm -f "$B"
cmake --build build/final --target CFDTurbulenceChannelValidationTests -j 16 2>&1 \
  | grep -E "error:|FAILED" | head -20
[ -x "$B" ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
"$B" --gtest_filter='SSTAutomaticWallChannel.*' 2>&1 | grep -v "^\[INFO\]"

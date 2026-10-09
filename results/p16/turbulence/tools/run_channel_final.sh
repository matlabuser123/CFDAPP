#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/integration/turbulence_channel/test_wall_function_channel.cpp
B=./build/final/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests
rm -f "$B"
cmake --build build/final --target CFDTurbulenceChannelValidationTests -j 16 2>&1 | grep -E "error:|FAILED" | head -20
[ -x "$B" ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
"$B" 2>&1 | grep -v "^\[INFO\]" | tail -22

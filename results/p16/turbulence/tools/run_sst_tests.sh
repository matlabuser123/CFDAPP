#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/unit/turbulence/test_sst_wall_functions.cpp
cmake -S . -B build/final > /dev/null 2>&1
B=./build/final/tests/unit/turbulence/CFDTurbulenceTests
rm -f "$B"
cmake --build build/final --target CFDTurbulenceTests -j 16 2>&1 | grep -E "error:|FAILED" | head -25
[ -x "$B" ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
"$B" --gtest_filter='SSTWallFunction*' 2>&1 | tail -40

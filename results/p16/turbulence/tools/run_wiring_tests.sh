#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/unit/turbulence/test_wall_function_wiring.cpp
cmake -S . -B build/final > /dev/null 2>&1
rm -f ./build/final/tests/unit/turbulence/CFDTurbulenceTests
cmake --build build/final --target CFDTurbulenceTests -j 16 2>&1 | grep -E "error:|FAILED" | head -25
[ -x ./build/final/tests/unit/turbulence/CFDTurbulenceTests ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
./build/final/tests/unit/turbulence/CFDTurbulenceTests --gtest_filter='WallFunctionWiring.*' 2>&1 | tail -35

#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/unit/thermal/test_convective_boundary.cpp
rm -f ./build/final/tests/unit/thermal/CFDThermalTests
cmake --build build/final --target CFDThermalTests -j 16 2>&1 | grep -E "error:|FAILED" | head -20
[ -x ./build/final/tests/unit/thermal/CFDThermalTests ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
./build/final/tests/unit/thermal/CFDThermalTests 2>&1 | tail -10

#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/integration/thermal/test_thermal_validation.cpp
rm -f ./build/final/tests/integration/thermal/CFDThermalValidationTests
cmake --build build/final --target CFDThermalValidationTests -j 16 2>&1 | grep -E "error:|FAILED" | head -20
[ -x ./build/final/tests/integration/thermal/CFDThermalValidationTests ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
./build/final/tests/integration/thermal/CFDThermalValidationTests 2>&1 | grep -v "^\[INFO\]" | tail -30

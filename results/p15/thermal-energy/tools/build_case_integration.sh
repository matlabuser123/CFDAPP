#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/integration/case/test_variable_property_production_case.cpp
cmake -S . -B build/final > /dev/null 2>&1
rm -f ./build/final/tests/integration/case/CFDCaseIntegrationTests
cmake --build build/final --target CFDCaseIntegrationTests -j 16 2>&1 | grep -E "error:|FAILED" | head -30
if [ ! -x ./build/final/tests/integration/case/CFDCaseIntegrationTests ]; then
  echo "BUILD FAILED"; exit 1
fi
echo "BUILD OK"
./build/final/tests/integration/case/CFDCaseIntegrationTests \
  --gtest_filter='VariablePropertyProductionCase.*' 2>&1 | tail -40

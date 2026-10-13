#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
rm -f ./build/final/tests/unit/thermal/CFDThermalTests \
      ./build/final/tests/integration/thermal/CFDThermalValidationTests
cmake --build build/final --target CFDThermalTests CFDThermalValidationTests -j 16 2>&1 \
  | grep -E "error:|FAILED" | head -20
[ -x ./build/final/tests/unit/thermal/CFDThermalTests ] || { echo "UNIT BUILD FAILED"; exit 1; }
[ -x ./build/final/tests/integration/thermal/CFDThermalValidationTests ] || { echo "VAL BUILD FAILED"; exit 1; }
echo "BUILD OK"
echo "=== thermal unit ==="
./build/final/tests/unit/thermal/CFDThermalTests 2>&1 | tail -12
echo "unit exit=$?"
echo "=== thermal validation ==="
./build/final/tests/integration/thermal/CFDThermalValidationTests 2>&1 | grep -v "^\[INFO\]" | tail -20

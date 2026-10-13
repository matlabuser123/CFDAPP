#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=./build/final/tests
echo "=== 1-2. encoder / device-plan rejection ==="
$B/unit/gpu/CFDGpuTests --gtest_filter='BoundaryEncoding.*' 2>&1 | tail -4
echo
echo "=== 3-6. thermal unit: BCs, properties, enthalpy, transient ==="
$B/unit/thermal/CFDThermalTests 2>&1 | tail -4
echo
echo "=== 7. CFDThermalValidationTests (first ever run) ==="
$B/integration/thermal/CFDThermalValidationTests 2>&1 | tail -30

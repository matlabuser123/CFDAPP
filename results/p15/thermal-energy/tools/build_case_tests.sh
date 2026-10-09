#!/usr/bin/env bash
# P15 item 1 -- case-file reachability. Build + run the io unit target.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/unit/io/test_thermal_property_case.cpp
cmake -S . -B build/final > /dev/null 2>&1
rm -f ./build/final/tests/unit/io/CFDIoTests
cmake --build build/final --target CFDIoTests -j 16 2>&1 | grep -E "error:|FAILED|Error" | head -30
if [ ! -x ./build/final/tests/unit/io/CFDIoTests ]; then
  echo "BUILD FAILED -- no binary"
  exit 1
fi
echo "BUILD OK"
echo "=== ThermalPropertyCase.* ==="
./build/final/tests/unit/io/CFDIoTests --gtest_filter='ThermalPropertyCase.*' 2>&1 | tail -45

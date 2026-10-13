#!/usr/bin/env bash
# P16b: build + run the automatic (blended) near-wall treatment unit tests.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i \
  include/cfd/turbulence/WallFunctions.hpp src/turbulence/WallFunctions.cpp \
  include/cfd/turbulence/WallTreatment.hpp src/turbulence/WallTreatment.cpp \
  include/cfd/turbulence/SSTModel.hpp src/turbulence/SSTModel.cpp \
  tests/unit/turbulence/test_automatic_wall_treatment.cpp
cmake -S . -B build/final > /dev/null 2>&1
rm -f ./build/final/tests/unit/turbulence/CFDTurbulenceTests
cmake --build build/final --target CFDTurbulenceTests -j 16 2>&1 | grep -E "error:|FAILED" | head -30
[ -x ./build/final/tests/unit/turbulence/CFDTurbulenceTests ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
./build/final/tests/unit/turbulence/CFDTurbulenceTests \
  --gtest_filter='AutomaticWallTreatment.*' 2>&1 | tail -45

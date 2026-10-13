#!/usr/bin/env bash
# P16b: full CPU build + the unit-level evidence for the automatic treatment.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i \
  include/cfd/turbulence/WallFunctions.hpp src/turbulence/WallFunctions.cpp \
  include/cfd/turbulence/WallTreatment.hpp src/turbulence/WallTreatment.cpp \
  include/cfd/turbulence/SSTModel.hpp src/turbulence/SSTModel.cpp \
  include/cfd/io/case/PhysicsConfig.hpp src/io/case/PhysicsConfigParser.cpp \
  src/io/CaseBuilder.cpp \
  tests/unit/turbulence/test_automatic_wall_treatment.cpp \
  tests/unit/turbulence/test_wall_treatment.cpp \n  tests/unit/turbulence/test_spalding_wall_law.cpp \n  tests/unit/turbulence/test_momentum_wall_flux.cpp \
  tests/unit/io/test_turbulence_case.cpp \
  tests/integration/turbulence_channel/test_wall_function_channel.cpp
cmake -S . -B build/final > /dev/null 2>&1
T=./build/final/tests/unit/turbulence/CFDTurbulenceTests
I=./build/final/tests/unit/io/CFDIoTests
rm -f "$T" "$I"
cmake --build build/final --target CFDTurbulenceTests CFDIoTests -j 16 2>&1 \
  | grep -E "error:|FAILED" | head -30
[ -x "$T" ] || { echo "TURBULENCE BUILD FAILED"; exit 1; }
[ -x "$I" ] || { echo "IO BUILD FAILED"; exit 1; }
echo "BUILD OK"
echo "=== turbulence unit suite ==="
"$T" 2>&1 | tail -6
echo "=== io unit suite ==="
"$I" 2>&1 | tail -6

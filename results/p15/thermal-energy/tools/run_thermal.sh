#!/usr/bin/env bash
# NOTE: the build result is CHECKED before running. An earlier version piped the
# build through grep and then ran the test binary regardless -- so a failed
# compile silently re-ran the previous build and reported 120 passing tests.
# Same failure as P13-007's unregistered smoke test and P14's stale probe.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/unit/thermal/test_thermal_transient.cpp \
  src/thermal/EnergyEquation.cpp include/cfd/thermal/EnergyEquation.hpp
cmake -S . -B build/final > /dev/null 2>&1
if ! cmake --build build/final --target CFDThermalTests -j 12 > /tmp/p15_build.log 2>&1; then
  echo "BUILD FAILED"; grep -E "error:" /tmp/p15_build.log | head -12; exit 1
fi
./build/final/tests/unit/thermal/CFDThermalTests 2>&1 | tail -8

#!/usr/bin/env bash
# Pyramid tier 1-2: build only the affected targets, run only the affected tests.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i src/thermal/EnergyEquation.cpp include/cfd/gpu/BoundaryEncoding.hpp \
  cuda/kernels/DeviceDiffusionPlan.cpp cuda/kernels/DeviceConvectionPlan.cpp \
  tests/unit/gpu/test_boundary_encoding.cpp tests/integration/thermal/test_thermal_validation.cpp 2>/dev/null
cmake -S . -B build/final > /dev/null 2>&1
if ! cmake --build build/final --target CFDThermalTests CFDGpuTests CFDThermalValidationTests -j 20 \
     > /tmp/p15_build.log 2>&1; then
  echo "BUILD FAILED"
  grep -E "error:" /tmp/p15_build.log | head -20
  exit 1
fi
echo "BUILD OK"

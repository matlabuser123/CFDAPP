#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i \
  include/cfd/gpu/DeviceTurbulence.hpp \
  include/cfd/turbulence/TurbulenceProduction.hpp \
  src/turbulence/TurbulenceProduction.cpp \
  src/turbulence/KEpsilonModel.cpp \
  tests/unit/gpu/test_device_turbulence_algebra.cpp
cmake -S . -B build/cuda -DCFDAPP_ENABLE_CUDA=ON > /dev/null 2>&1
cmake --build build/cuda --target CFDGpuTests CFDTurbulenceTests -j 16 2>&1 \
  | grep -E "error|Error|FAILED" | head -40
echo "--- build finished ---"
./build/cuda/tests/unit/gpu/CFDGpuTests --gtest_filter='DeviceTurbulenceAlgebra.*' 2>&1 | tail -45

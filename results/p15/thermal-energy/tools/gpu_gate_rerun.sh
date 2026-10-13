#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i include/cfd/gpu/BoundaryEncoding.hpp \
  cuda/kernels/DeviceGradientPlan.cpp cuda/kernels/DeviceBoundaryConditions.cpp \
  cuda/kernels/DeviceDiffusionPlan.cpp cuda/kernels/DeviceConvectionPlan.cpp
cmake --build build/final --target cfdcuda CFDGpuTests -j 16 2>&1 | grep -E "error:|FAILED" | head -20
echo "--- build done ---"
./build/final/tests/unit/gpu/CFDGpuTests --gtest_filter='BoundaryEncoding.*' 2>&1 | tail -4

#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i include/cfd/boundary/ConvectiveHeatTransfer.hpp src/boundary/ConvectiveHeatTransfer.cpp tests/unit/thermal/test_convective_boundary.cpp include/cfd/boundary/BoundaryCondition.hpp src/discretization/NonOrthogonalDiffusion.cpp
cmake -S . -B build/final > /dev/null 2>&1
cmake --build build/final --target CFDThermalTests -j 12 2>&1 | grep -E "error:|FAILED" | head -15
./build/final/tests/unit/thermal/CFDThermalTests --gtest_filter='ConvectiveBoundary.*' 2>&1 | tail -8

#!/usr/bin/env bash
# P15 GPU gates (unblocked by the driver restore):
#   1. plan-level encoder criteria 3-5
#   2. ConvectiveHeatTransfer rejected gracefully, naming runtime conductivity
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
echo "=== building the CUDA library and the GPU unit tests ==="
cmake --build build/final --target cfdcuda CFDGpuTests -j 16 2>&1 | grep -E "error:|FAILED" | head -20
echo "--- build done ---"
echo
echo "=== encoder unit gate (criteria 1-2, 6-7) ==="
./build/final/tests/unit/gpu/CFDGpuTests --gtest_filter='BoundaryEncoding.*' 2>&1 | tail -6

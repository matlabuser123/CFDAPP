#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i \
  include/cfd/gpu/DeviceScalarTransport.hpp \
  tests/unit/gpu/test_device_scalar_transport_semantics.cpp
cmake -S . -B build/cuda -DCFDAPP_ENABLE_CUDA=ON > /dev/null 2>&1
cmake --build build/cuda --target CFDGpuTests -j 16 2>&1 | grep -E "error|Error|FAILED|warning" | head -40
echo "--- build finished ---"

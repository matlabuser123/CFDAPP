#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/unit/transport/test_scalar_transport.cpp src/transport/ScalarTransport.cpp include/cfd/transport/ScalarTransport.hpp
cmake --build build/final --target CFDTransportTests -j 12 2>&1 | grep -E "error:" | head -10
cmake --build build/final --target CFDTransportTests -j 12 2>&1 | tail -1
./build/final/tests/unit/transport/CFDTransportTests 2>&1 | tail -8

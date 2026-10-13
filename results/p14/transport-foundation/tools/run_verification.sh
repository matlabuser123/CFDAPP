#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/integration/transport/test_transport_verification.cpp
cmake --build build/final --target CFDTransportVerificationTests -j 12 2>&1 | grep -E "error:|FAILED" | head -20
cmake --build build/final --target CFDTransportVerificationTests -j 12 2>&1 | tail -2
./build/final/tests/integration/transport/CFDTransportVerificationTests 2>&1 | tail -35

#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/unit/transport/test_scalar_transport.cpp
cmake --build build/final -j 12 2>&1 | grep -E "error:|FAILED" | head -25
cmake --build build/final -j 12 2>&1 | tail -2
cd build/final && ctest -R "Transport|TimeDerivativeFieldDensity" --output-on-failure 2>&1 | tail -40

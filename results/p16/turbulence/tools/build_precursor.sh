#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/integration/backward_facing_step/DriverSeegmillerCase.hpp tests/integration/backward_facing_step/DriverSeegmillerPrecursor.cpp
cmake -S . -B build/final > /dev/null 2>&1
cmake --build build/final --target CFDBackwardFacingStepTests -j 16 2>&1 | grep -E "error:|FAILED" | head -25
echo "--- build finished ---"

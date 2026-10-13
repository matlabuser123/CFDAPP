#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/integration/backward_facing_step/test_sst_separation.cpp
cmake -S . -B build/final > /dev/null 2>&1
B=./build/final/tests/integration/backward_facing_step/CFDBackwardFacingStepTests
rm -f "$B"
cmake --build build/final --target CFDBackwardFacingStepTests -j 16 2>&1 | grep -E "error:|FAILED" | head -25
[ -x "$B" ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
timeout 3000 "$B" --gtest_filter='SSTSeparation.*' 2>&1 | grep -v "^\[INFO\]" | tail -40
echo "runner exit: $?"

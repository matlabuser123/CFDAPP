#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/integration/backward_facing_step/test_driver_seegmiller.cpp
B=./build/final/tests/integration/backward_facing_step/CFDBackwardFacingStepTests
rm -f "$B"
cmake --build build/final --target CFDBackwardFacingStepTests -j 16 2>&1 | grep -E "error:|FAILED" | head -20
[ -x "$B" ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
timeout 7200 "$B" --gtest_filter="DriverSeegmiller.CorrectedMeshSequence" 2>&1 | grep -v "^\[INFO\]" | tail -35

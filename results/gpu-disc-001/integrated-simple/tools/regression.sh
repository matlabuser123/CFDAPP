#!/usr/bin/env bash
# GPU-DISC-001M Step 4 -- full regression, with build freshness and the exact
# build identity recorded BEFORE and AFTER ctest so the suite cannot have run
# against a stale binary.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/integrated-simple
cd "$ROOT"
FRESH=$EVID/regression_freshness.log
: > "$FRESH"
SOURCES="include/cfd/gpu/GpuSimpleDiscretization.hpp
src/gpu/GpuSimpleDiscretization.cpp
cuda/kernels/GpuSimpleDiscretizationCuda.cpp
src/pressure_velocity/SIMPLE.cpp
include/cfd/pressure_velocity/SIMPLESettings.hpp
include/cfd/pressure_velocity/SIMPLEResult.hpp
cuda/CMakeLists.txt
src/CMakeLists.txt"
{
  echo "=== test command ==="
  echo "ctest --test-dir build/cuda --output-on-failure"
  echo ""
  echo "=== build identity BEFORE ctest ==="
  ninja -C build/cuda
  sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a
  echo ""
  echo "=== 001M source sha256 BEFORE ctest ==="
  sha256sum $SOURCES
} >> "$FRESH" 2>&1
ctest --test-dir build/cuda --output-on-failure > "$EVID/regression.log" 2>&1
RC=$?
{
  echo ""
  echo "=== build identity AFTER ctest ==="
  ninja -C build/cuda
  sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a
  echo ""
  echo "=== 001M source sha256 AFTER ctest ==="
  sha256sum $SOURCES
  echo ""
  echo "ctest exit=$RC"
} >> "$FRESH" 2>&1
grep -E "tests passed|tests failed|Total Test time" "$EVID/regression.log"
echo "--- freshness ---"
grep -E "no work to do|ctest exit" "$FRESH"
exit $RC

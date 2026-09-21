#!/usr/bin/env bash
# GPU-DISC-001Q -- full regression on the production build.
#
# This gate CHANGED PRODUCTION CODE (stage timers in SIMPLE.cpp/SIMPLEResult.hpp,
# device-byte counters in GPUExecutionStats.hpp/DeviceBuffer.hpp), so the full
# suite has to re-pass, with build freshness proven before AND after ctest.
#
# build/omp is a SEPARATE directory created only to measure an OpenMP CPU
# baseline; the production build/cuda is what ships and what is tested here.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/performance/regression
mkdir -p "$EVID"
cd "$ROOT"

SOURCES="src/pressure_velocity/SIMPLE.cpp
include/cfd/pressure_velocity/SIMPLEResult.hpp
include/cfd/gpu/GPUExecutionStats.hpp
include/cfd/gpu/DeviceBuffer.hpp"

FRESH=$EVID/regression_freshness.log
: > "$FRESH"
{
  echo "=== test command ==="
  echo "ctest --test-dir build/cuda --output-on-failure"
  echo ""
  echo "=== build identity BEFORE ctest ==="
  ninja -C build/cuda
  sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a
  echo ""
  echo "=== the four files this gate changed, BEFORE ctest ==="
  sha256sum $SOURCES
} >> "$FRESH" 2>&1

echo "=== 15 GPU-DISC differential gates (instrumentation must not have moved numerics) ==="
bash "$ROOT/results/gpu-disc-001/full-solve-equivalence/tools/all_gates.sh" \
  > "$EVID/all_gates.log" 2>&1
gatesRc=$?
tail -3 "$EVID/all_gates.log"
echo "  rc=$gatesRc"

echo ""
echo "=== full regression ==="
ctest --test-dir build/cuda --output-on-failure > "$EVID/regression.log" 2>&1
RC=$?
{
  echo ""
  echo "=== build identity AFTER ctest ==="
  ninja -C build/cuda
  sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a
  echo ""
  echo "=== the four files this gate changed, AFTER ctest ==="
  sha256sum $SOURCES
  echo ""
  echo "ctest exit=$RC"
} >> "$FRESH" 2>&1

grep -E "tests passed|tests failed|Total Test time" "$EVID/regression.log"
echo "--- freshness ---"
grep -E "no work to do|ctest exit" "$FRESH"

[ $gatesRc -ne 0 ] && exit 1
exit $RC

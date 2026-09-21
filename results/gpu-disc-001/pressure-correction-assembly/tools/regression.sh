#!/usr/bin/env bash
# GPU-DISC-001I -- full regression, with build freshness proved BEFORE and
# AFTER ctest so the suite cannot have run against a stale binary.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/pressure-correction-assembly
cd "$ROOT"

FRESH=$EVID/regression_freshness.log
: > "$FRESH"

{
  echo "=== sha256 of the 001I sources, BEFORE ctest ==="
  sha256sum include/cfd/gpu/DevicePressureCorrection.hpp \
            cuda/kernels/DevicePressureCorrectionPlan.cpp \
            cuda/kernels/DevicePressureCorrectionKernel.cu \
            cuda/CMakeLists.txt
  echo ""
  echo "=== ninja freshness BEFORE ctest ==="
  ninja -C build/cuda
} >> "$FRESH" 2>&1

ctest --test-dir build/cuda --output-on-failure > "$EVID/regression.log" 2>&1
RC=$?

{
  echo ""
  echo "=== ninja freshness AFTER ctest ==="
  ninja -C build/cuda
  echo ""
  echo "=== sha256 of the 001I sources, AFTER ctest ==="
  sha256sum include/cfd/gpu/DevicePressureCorrection.hpp \
            cuda/kernels/DevicePressureCorrectionPlan.cpp \
            cuda/kernels/DevicePressureCorrectionKernel.cu \
            cuda/CMakeLists.txt
  echo ""
  echo "ctest exit=$RC"
} >> "$FRESH" 2>&1

tail -6 "$EVID/regression.log"
echo "--- freshness ---"
grep -E "no work to do|work to do|ctest exit" "$FRESH"
exit $RC

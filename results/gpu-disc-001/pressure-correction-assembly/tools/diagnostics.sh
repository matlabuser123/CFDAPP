#!/usr/bin/env bash
# GPU-DISC-001I -- full differential + compute-sanitizer diagnostics.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/pressure-correction-assembly
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

mkdir -p "$EVID/differential" "$EVID/cuda-diagnostics"

echo "=== freshness before ==="
ninja -C build/cuda

echo "=== rebuild harness ==="
/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o /tmp/pcorr_equiv "$EVID/tools/pressure_correction_equivalence.cpp" \
  build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -L"$CUDA/lib64" -lcudart || exit 1

echo "=== full differential ==="
/tmp/pcorr_equiv > "$EVID/differential/differential.log" 2>&1
echo "differential exit=$?"
tail -3 "$EVID/differential/differential.log"

for tool in memcheck initcheck synccheck racecheck; do
  echo "=== compute-sanitizer $tool ==="
  "$CUDA/bin/compute-sanitizer" --tool "$tool" /tmp/pcorr_equiv --quick \
    > "$EVID/cuda-diagnostics/$tool.log" 2>&1
  echo "exit=$?"
  grep -E "ERROR SUMMARY|PRESSURE CORRECTION EQUIVALENCE" "$EVID/cuda-diagnostics/$tool.log" | tail -3
done

echo "=== sha256 of the shipped sources ==="
sha256sum include/cfd/gpu/DevicePressureCorrection.hpp \
          cuda/kernels/DevicePressureCorrectionPlan.cpp \
          cuda/kernels/DevicePressureCorrectionKernel.cu \
          "$EVID/tools/pressure_correction_equivalence.cpp"

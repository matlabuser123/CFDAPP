#!/usr/bin/env bash
# GPU-DISC-001J -- build and run the velocity-correction differential harness.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/velocity-correction
CUDA=/usr/local/cuda-12.9
OUT=${OUT:-/tmp/vcorr_equiv}
cd "$ROOT"

ninja -C build/cuda > /tmp/ninja.log 2>&1 || { grep -vE '^\[[0-9]+/[0-9]+\]' /tmp/ninja.log | head -40; exit 1; }
tail -1 /tmp/ninja.log

/usr/bin/c++ \
  -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o "$OUT" "$EVID/tools/velocity_correction_equivalence.cpp" \
  build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a \
  -L"$CUDA/lib64" -lcudart || exit 1

echo "built $OUT"
"$OUT" "$@"

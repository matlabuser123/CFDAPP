#!/usr/bin/env bash
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$ROOT/results/gpu-disc-001/performance
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o /tmp/gpu_disc_stage "$P/tools/stage_profiler.cpp" \
  -Wl,--start-group build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

for n in 320 640; do
  echo "########## ${n}x${n} ##########"
  /tmp/gpu_disc_stage "$n" 20 | tee "$P/profiling/stage_profile_${n}.log"
  echo ""
done

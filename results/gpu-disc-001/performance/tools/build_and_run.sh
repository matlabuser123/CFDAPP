#!/usr/bin/env bash
# GPU-DISC-001Q -- build and run the performance benchmark.
#
# Release libraries, the production build, no special flags. The benchmark
# translation unit itself is -O2 -DNDEBUG like every other harness in this
# project; it contains no numerics, only timing and bookkeeping.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CUDA=/usr/local/cuda-12.9
SRC=$ROOT/results/gpu-disc-001/performance/tools/performance_benchmark.cpp
BIN=${BIN:-/tmp/gpu_disc_perf}
cd "$ROOT"

ninja -C build/cuda > /tmp/ninja_perf.log 2>&1 || {
  grep -vE '^\[[0-9]+/[0-9]+\]' /tmp/ninja_perf.log | head -40; exit 1; }
tail -1 /tmp/ninja_perf.log

# SIMPLE.cpp (cfdcore) calls into cfdcuda, so the archives are mutually
# dependent and need a link group rather than an ordering.
/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o "$BIN" "$SRC" \
  -Wl,--start-group build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

echo "built $BIN"
echo "OMP_NUM_THREADS=${OMP_NUM_THREADS:-<unset, defaults to nproc>}"
"$BIN" "$@"

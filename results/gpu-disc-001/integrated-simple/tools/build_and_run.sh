#!/usr/bin/env bash
# GPU-DISC-001M -- build and run the integrated production SIMPLE harness.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/integrated-simple
CUDA=/usr/local/cuda-12.9
OUT=${OUT:-/tmp/integrated_simple}
cd "$ROOT"

ninja -C build/cuda > /tmp/ninja.log 2>&1 || { grep -vE '^\[[0-9]+/[0-9]+\]' /tmp/ninja.log | head -40; exit 1; }
tail -1 /tmp/ninja.log

# SIMPLE.cpp (cfdcore) calls into cfdcuda, so the archives are mutually
# dependent and need a link group rather than an order.
/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o "$OUT" "$EVID/tools/integrated_simple_equivalence.cpp" \
  -Wl,--start-group build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

echo "built $OUT"
"$OUT" "$@"

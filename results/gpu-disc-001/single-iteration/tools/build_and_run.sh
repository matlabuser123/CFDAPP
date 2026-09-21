#!/usr/bin/env bash
# GPU-DISC-001L -- build and run the single-iteration differential harness.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/single-iteration
CUDA=/usr/local/cuda-12.9
OUT=${OUT:-/tmp/singleiter_equiv}
cd "$ROOT"

ninja -C build/cuda > /tmp/ninja.log 2>&1 || { grep -vE '^\[[0-9]+/[0-9]+\]' /tmp/ninja.log | head -40; exit 1; }
tail -1 /tmp/ninja.log

# SIMPLE.cpp (in cfdcore) calls into cfdcuda's residency manager, so the two
# archives are mutually dependent: a single pass in either order leaves symbols
# undefined. They need a link group, not an order.
/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o "$OUT" "$EVID/tools/single_iteration_equivalence.cpp" \
  -Wl,--start-group build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

echo "built $OUT"
"$OUT" "$@"

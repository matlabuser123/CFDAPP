#!/usr/bin/env bash
# GPU-DISC-001O -- build the diagnostic workload.
#
# -lineinfo is added so a sanitizer report names a source line. It changes no
# numerical flag: -fmad=false, -O3 and -DNDEBUG are untouched, so this build IS
# the production build (see environment.txt).
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/cuda-diagnostics
CUDA=/usr/local/cuda-12.9
OUT=${OUT:-/tmp/gpu_diag_workload}
cd "$ROOT"

ninja -C build/cuda > /tmp/ninja.log 2>&1 || { grep -vE '^\[[0-9]+/[0-9]+\]' /tmp/ninja.log | head -40; exit 1; }
tail -1 /tmp/ninja.log

/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra -g \
  -o "$OUT" "$EVID/tools/diagnostic_workload.cpp" \
  -Wl,--start-group build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

echo "built $OUT"
"$OUT" "$@"

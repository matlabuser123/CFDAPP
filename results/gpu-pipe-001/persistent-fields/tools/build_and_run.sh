#!/usr/bin/env bash
# GPU-PIPE-001 persistent fields -- build and run a harness against build/final.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CUDA=/usr/local/cuda-12.9
BUILD=${BUILD:-$ROOT/build/final}
SRC=$1; shift
BIN=/tmp/$(basename "$SRC" .cpp)
cd "$ROOT"

ninja -C "$BUILD" > /tmp/ninja_pf.log 2>&1 || {
  grep -vE '^\[[0-9]+/[0-9]+\]' /tmp/ninja_pf.log | head -40; exit 1; }
tail -1 /tmp/ninja_pf.log

/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o "$BIN" "$SRC" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

"$BIN" "$@"

#!/usr/bin/env bash
# GPU-DISC-001I -- build and run the pressure-correction differential harness.
# Run from the repository root inside WSL (Ubuntu-22.04).
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)
cd "$ROOT"

BUILD=build/cuda
SRC=results/gpu-disc-001/pressure-correction-assembly/tools/pressure_correction_equivalence.cpp
OUT=${OUT:-/tmp/pcorr_equiv}
CUDA=/usr/local/cuda-12.9

ninja -C "$BUILD" | tail -2

/usr/bin/c++ \
  -I"$ROOT/include" \
  -I"$ROOT/$BUILD/generated/include" \
  -I"$ROOT/$BUILD/_deps/nlohmann_json-src/include" \
  -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o "$OUT" "$SRC" \
  "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" \
  -L"$CUDA/lib64" -lcudart

echo "built $OUT"
"$OUT" "$@"

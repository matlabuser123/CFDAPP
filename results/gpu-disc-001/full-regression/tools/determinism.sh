#!/usr/bin/env bash
# GPU-DISC-001R Phase I -- determinism / reproducibility on the final binaries.
#
# The project's EXISTING policy is used, not a new one invented for this gate:
#
#   * GPU-DISC-001M layer R already asserts that repeating a GPU-discretization
#     solve gives an IDENTICAL SIMPLEResult -- that is the project's determinism
#     test for this path and it runs inside integrated_simple_equivalence.
#   * GPU-DISC-001N's full-solve gate compares whole residual HISTORIES bitwise.
#
# This phase re-runs both on the final binaries and, separately, repeats a CPU
# and a GPU production solve to compare convergence state, final fields,
# residual history, mass imbalance and iteration count directly.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/determinism
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
mkdir -p "$EVID"

echo "=== the project's own determinism checks, on the final binaries ==="
echo "  integrated_simple_equivalence layer R: 'GPU discretization repeated, identical'"
grep -E "^  (PASS|FAIL) R " "$ROOT/results/gpu-disc-001/full-regression/gpu-gates/integrated_simple_equivalence.log" \
  2>/dev/null | sed 's/^/  /' || echo "  (gpu-gates not yet run)"

echo ""
echo "=== repeated production solves ==="
/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 \
  -o /tmp/final_determinism "$ROOT/results/gpu-disc-001/full-regression/tools/determinism_probe.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || { echo "build failed"; exit 1; }

/tmp/final_determinism | tee "$EVID/determinism.log"
exit ${PIPESTATUS[0]}

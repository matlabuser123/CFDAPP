#!/usr/bin/env bash
# GPU-DISC-001R Phase: has the known GPU BiCGSTAB restart asymmetry BROADENED?
#
# TODO.md records the reproducer:
#
#   2D cavity 40x40, outer tolerance 1e-6, outer budget 3000
#   GPU: PressureCorrectionFailure, BiCGSTAB breakdown after 74 iterations
#   CPU: continues the full outer budget
#
# Present at baseline 548401a; NOT introduced by GPU-PIPE-001 and NOT to be
# fixed here. The question this gate must answer is narrower and specific:
# did GPU-DISC-001 make it WORSE or WIDER? Two things would count as worse:
#   * the GPU-DISC path failing where the old GPU path did not;
#   * the failure appearing on a case that was previously healthy.
#
# So the reproducer is run on THREE arms and compared, not just re-observed.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/validation
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
mkdir -p "$EVID"

/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 \
  -o /tmp/final_debt "$ROOT/results/gpu-disc-001/full-regression/tools/known_debt_probe.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

/tmp/final_debt | tee "$EVID/known_debt.log"
exit ${PIPESTATUS[0]}

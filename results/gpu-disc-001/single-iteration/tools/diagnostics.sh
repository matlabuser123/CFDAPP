#!/usr/bin/env bash
# GPU-DISC-001L -- compute-sanitizer over the one-iteration CUDA path.
# Each tool must be NON-VACUOUS: the log must contain the harness's PASS line.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/single-iteration
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

mkdir -p "$EVID/cuda-diagnostics"

echo "=== build freshness ==="
ninja -C build/cuda 2>&1 | tail -1

/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o /tmp/singleiter_equiv "$EVID/tools/single_iteration_equivalence.cpp" \
  -Wl,--start-group build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

fail=0
for tool in memcheck initcheck synccheck racecheck; do
  echo "=== compute-sanitizer $tool ==="
  "$CUDA/bin/compute-sanitizer" --tool "$tool" /tmp/singleiter_equiv --quick \
    > "$EVID/cuda-diagnostics/$tool.log" 2>&1
  rc=$?
  if grep -q "SINGLE ITERATION EQUIVALENCE: PASS" "$EVID/cuda-diagnostics/$tool.log"; then
    vac="non-vacuous"
  else
    vac="VACUOUS"
    fail=1
  fi
  summary=$(grep -E "ERROR SUMMARY|RACECHECK SUMMARY" "$EVID/cuda-diagnostics/$tool.log" | tail -1)
  echo "  exit=$rc  $vac  $summary"
  case "$summary" in
    *"0 errors"*) ;;
    *"0 hazards"*) ;;
    *) fail=1 ;;
  esac
done

echo ""
if [ $fail -eq 0 ]; then echo "CUDA DIAGNOSTICS: 4/4 clean and non-vacuous"; else echo "CUDA DIAGNOSTICS: FAIL"; fi
exit $fail

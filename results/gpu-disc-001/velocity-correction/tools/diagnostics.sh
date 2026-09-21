#!/usr/bin/env bash
# GPU-DISC-001J Step 2 -- compute-sanitizer over the velocity-correction path.
#
# Each tool must be NON-VACUOUS: the log has to contain the harness's own PASS
# line, proving the CUDA path actually executed rather than the run dying early.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/velocity-correction
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

mkdir -p "$EVID/cuda-diagnostics"

echo "=== build freshness ==="
ninja -C build/cuda 2>&1 | tail -1

/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o /tmp/vcorr_equiv "$EVID/tools/velocity_correction_equivalence.cpp" \
  build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -L"$CUDA/lib64" -lcudart || exit 1

fail=0
for tool in memcheck initcheck synccheck racecheck; do
  echo "=== compute-sanitizer $tool ==="
  "$CUDA/bin/compute-sanitizer" --tool "$tool" /tmp/vcorr_equiv --quick \
    > "$EVID/cuda-diagnostics/$tool.log" 2>&1
  rc=$?
  # Non-vacuity: the harness's own verdict must be in the log.
  if grep -q "VELOCITY CORRECTION EQUIVALENCE: PASS" "$EVID/cuda-diagnostics/$tool.log"; then
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

#!/usr/bin/env bash
# GPU-DISC-001O -- diagnostic repeatability.
#
# The primary memcheck workload twice, and racecheck twice on the GPU-solver
# mode (the only one with shared memory, so the only one where an intermittent
# hazard could hide). Same exit status, same summary, same workload result.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/cuda-diagnostics/repeatability
CUDA=/usr/local/cuda-12.9
BIN=/tmp/gpu_diag_workload
cd "$ROOT"
mkdir -p "$EVID"

fail=0
run() {
  local tool=$1 mode=$2 pass=$3
  local log="$EVID/${tool}-${mode}-run${pass}.log"
  "$CUDA/bin/compute-sanitizer" --tool "$tool" "$BIN" "$mode" > "$log" 2>&1
  local rc=$?
  local summary
  summary=$(grep -E "ERROR SUMMARY|RACECHECK SUMMARY" "$log" | tail -1)
  local result
  result=$(grep -E "^DIAGNOSTIC WORKLOAD:" "$log" | tail -1)
  local kernels
  kernels=$(grep -oE "kernelLaunches=[0-9]+" "$log" | tail -1)
  echo "$rc|$summary|$result|$kernels"
}

for pair in "memcheck cavity2d" "memcheck gpusolver" "racecheck gpusolver"; do
  set -- $pair
  tool=$1; mode=$2
  a=$(run "$tool" "$mode" 1)
  b=$(run "$tool" "$mode" 2)
  if [ "$a" = "$b" ]; then
    verdict="IDENTICAL"
  else
    verdict="DIFFERED -- INTERMITTENT"
    fail=1
  fi
  echo "$tool $mode:"
  echo "  run 1: $a"
  echo "  run 2: $b"
  echo "  -> $verdict"
done

echo ""
if [ $fail -eq 0 ]; then
  echo "DIAGNOSTIC REPEATABILITY: identical across repeats"
else
  echo "DIAGNOSTIC REPEATABILITY: FAIL -- intermittent behaviour is a failed gate"
fi
exit $fail

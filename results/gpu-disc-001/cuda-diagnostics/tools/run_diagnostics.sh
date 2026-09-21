#!/usr/bin/env bash
# GPU-DISC-001O -- all four sanitizers over the production workload matrix.
#
# Every run preserves: the exact command, the mode, the exit code, the sanitizer
# summary, and the workload's own non-vacuity line. A tool whose log does not
# contain "PRODUCTION GPU PATH EXERCISED" examined nothing and is recorded as
# VACUOUS regardless of what its summary says.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/cuda-diagnostics
CUDA=/usr/local/cuda-12.9
BIN=/tmp/gpu_diag_workload
cd "$ROOT"

MODES="cavity2d inletoutlet2d case3d schemes nonorthogonal gpusolver"
declare -A LOGNAME=(
  [cavity2d]=cavity-2d
  [inletoutlet2d]=inlet-outlet-2d
  [case3d]=case-3d
  [schemes]=schemes
  [nonorthogonal]=nonorthogonal
  [gpusolver]=gpusolver
)

fail=0
for tool in memcheck initcheck synccheck racecheck; do
  mkdir -p "$EVID/$tool"
  for mode in $MODES; do
    log="$EVID/$tool/${LOGNAME[$mode]}.log"
    cmd="$CUDA/bin/compute-sanitizer --tool $tool $BIN $mode"
    {
      echo "# command: $cmd"
      echo "# mode:    $mode"
      echo "# build:   $(sha256sum build/cuda/cuda/libcfdcuda.a | cut -d' ' -f1)"
      echo ""
    } > "$log"
    $cmd >> "$log" 2>&1
    rc=$?
    echo "# exit code: $rc" >> "$log"

    summary=$(grep -E "ERROR SUMMARY|RACECHECK SUMMARY" "$log" | tail -1)
    if grep -q "PRODUCTION GPU PATH EXERCISED" "$log"; then vac="non-vacuous"; else vac="VACUOUS"; fail=1; fi
    kernels=$(grep -oE "kernelLaunches=[0-9]+" "$log" | tail -1)
    case "$summary" in
      *"0 errors"*) ;;
      *"0 hazards"*) ;;
      *) fail=1 ;;
    esac
    printf "%-10s %-16s rc=%-3s %-12s %-24s %s\n" "$tool" "$mode" "$rc" "$vac" "$kernels" "$summary"
  done
done

echo ""
if [ $fail -eq 0 ]; then
  echo "CUDA DIAGNOSTICS: all tools clean and non-vacuous over the full workload matrix"
else
  echo "CUDA DIAGNOSTICS: FAIL"
fi
exit $fail

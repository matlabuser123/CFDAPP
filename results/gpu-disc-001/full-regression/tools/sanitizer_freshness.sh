#!/usr/bin/env bash
# GPU-DISC-001R Phase J -- is the CUDA diagnostic evidence still valid?
#
# The brief's rule: "Do not claim sanitizer coverage from binaries that predate
# later source changes." The GPU-DISC-001P campaign re-ran all four sanitizers,
# and then GPU-DISC-001Q changed FOUR production files:
#
#   src/pressure_velocity/SIMPLE.cpp               stage timers
#   include/cfd/pressure_velocity/SIMPLEResult.hpp StageSeconds
#   include/cfd/gpu/GPUExecutionStats.hpp          device-byte counters
#   include/cfd/gpu/DeviceBuffer.hpp               maintains those counters
#
# Two of those touch GPU MEMORY accounting directly (DeviceBuffer's resize and
# release), which is exactly the category the brief says forces a rerun. So the
# sanitizers are re-run on the FINAL clean build rather than referenced.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/sanitizer-freshness
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
BIN=/tmp/final_diag_workload
cd "$ROOT"
mkdir -p "$EVID"

echo "=== why a rerun is required, not a reference ==="
echo "  GPU-DISC-001Q changed DeviceBuffer.hpp (resize/release) after the 001P"
echo "  sanitizer campaign. That is GPU memory ownership -- rerun."
sha256sum include/cfd/gpu/DeviceBuffer.hpp include/cfd/gpu/GPUExecutionStats.hpp \
  src/pressure_velocity/SIMPLE.cpp include/cfd/pressure_velocity/SIMPLEResult.hpp | sed 's/^/  /'

echo ""
echo "=== build the 001O workload against the FINAL clean build ==="
/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -g -std=c++20 \
  -o "$BIN" "$ROOT/results/gpu-disc-001/cuda-diagnostics/tools/diagnostic_workload.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
echo "  built $BIN"
sha256sum "$BUILD/cuda/libcfdcuda.a" | sed 's/^/  /'

MODES="cavity2d case3d nonorthogonal gpusolver"
fail=0
echo ""
printf "%-11s %-15s %-4s %-13s %-9s %s\n" tool mode rc vacuity kernels summary
for tool in memcheck initcheck synccheck racecheck; do
  for mode in $MODES; do
    log="$EVID/${tool}-${mode}.log"
    {
      echo "# command: compute-sanitizer --tool $tool $BIN $mode"
      echo "# build:   FINAL clean build/final"
      echo "# lib:     $(sha256sum "$BUILD/cuda/libcfdcuda.a" | cut -d' ' -f1)"
      echo ""
    } > "$log"
    "$CUDA/bin/compute-sanitizer" --tool "$tool" "$BIN" "$mode" >> "$log" 2>&1
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
    [ $rc -ne 0 ] && fail=1
    printf "%-11s %-15s %-4s %-13s %-9s %s\n" "$tool" "$mode" "$rc" "$vac" \
      "${kernels#kernelLaunches=}" "$summary"
  done
done

echo ""
if [ $fail -eq 0 ]; then
  echo "SANITIZER FRESHNESS: PASS -- 16/16 clean and non-vacuous on the FINAL build"
else
  echo "SANITIZER FRESHNESS: FAIL"
fi
exit $fail

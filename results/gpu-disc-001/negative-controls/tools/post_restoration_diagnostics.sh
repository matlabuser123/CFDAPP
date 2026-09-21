#!/usr/bin/env bash
# GPU-DISC-001P -- CUDA diagnostics AFTER the mutation campaign.
#
# The campaign mutated indexing (g4 off-by-one, k4/k7 next-face indices),
# buffer layout (h9b packed 2D least-squares), memory ownership (m3/m4/m5/n8
# skipped uploads) and 2D/3D buffer contracts (d12/d13/e5/h8), so the brief's
# rule applies: the repository must not be left in a state where the sanitizers
# were only run BEFORE a mutation campaign.
#
# This re-runs the GPU-DISC-001O workload on the RESTORED tree. It is the same
# workload, the same build flags and the same non-vacuity rule -- a tool whose
# log lacks "PRODUCTION GPU PATH EXERCISED" examined nothing and is VACUOUS
# whatever its summary says.
#
# Modes are chosen to cover exactly the contracts the mutations touched:
#   cavity2d       2D indexing, the reference pin, multi-iteration buffer reuse
#   case3d         U/V/W, the 3D cofactor path, 3D BC storage  (d13, e5, h9c)
#   schemes        the packed 2D least-squares layout           (h9b)
#   nonorthogonal  oblique-Neumann and skew geometry            (b4, h11)
#   gpusolver      the only kernels with shared memory and barriers
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/negative-controls/cuda-diagnostics
CUDA=/usr/local/cuda-12.9
BIN=/tmp/gpu_diag_workload_post
cd "$ROOT"
mkdir -p "$EVID"

echo "=== build identity (the RESTORED tree) ==="
ninja -C build/cuda 2>&1 | tail -1
sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a | tee "$EVID/build-identity.txt"

echo ""
echo "=== build the 001O diagnostic workload against the restored libraries ==="
/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -g -std=c++20 \
  -o "$BIN" "$ROOT/results/gpu-disc-001/cuda-diagnostics/tools/diagnostic_workload.cpp" \
  -Wl,--start-group build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
echo "built $BIN"

MODES="cavity2d case3d schemes nonorthogonal gpusolver"
fail=0
echo ""
printf "%-11s %-15s %-4s %-13s %-9s %s\n" tool mode rc vacuity kernels summary
for tool in memcheck initcheck synccheck racecheck; do
  for mode in $MODES; do
    log="$EVID/${tool}-${mode}.log"
    cmd="$CUDA/bin/compute-sanitizer --tool $tool $BIN $mode"
    {
      echo "# command: $cmd"
      echo "# mode:    $mode"
      echo "# tree:    RESTORED after the GPU-DISC-001P mutation campaign"
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
    [ $rc -ne 0 ] && fail=1
    printf "%-11s %-15s %-4s %-13s %-9s %s\n" "$tool" "$mode" "$rc" "$vac" "${kernels#kernelLaunches=}" "$summary"
  done
done

echo ""
if [ $fail -eq 0 ]; then
  echo "POST-RESTORATION CUDA DIAGNOSTICS: PASS -- 20/20 runs clean and non-vacuous"
else
  echo "POST-RESTORATION CUDA DIAGNOSTICS: FAIL"
fi
exit $fail

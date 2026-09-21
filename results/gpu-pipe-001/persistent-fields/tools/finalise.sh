#!/usr/bin/env bash
# GPU-PIPE-001 Persistent Fields -- everything after the negative controls.
#
#   1. CUDA diagnostics on the residency path (lifetimes, stale pointers,
#      buffers reused after resize, 2D/3D contracts -- the categories the brief
#      singles out, and exactly what a residency change can break)
#   2. CPU backend, from a CUDA-disabled build
#   3. performance 160/320/640, before vs after
#   4. full repository regression
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/persistent-fields
T=$E/tools
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
declare -A RC

echo "##################### 1. CUDA diagnostics on the residency path #####################"
/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -g -std=c++20 -o /tmp/pf_diag \
  "$ROOT/results/gpu-disc-001/cuda-diagnostics/tools/diagnostic_workload.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
fail=0
printf "%-11s %-15s %-4s %-13s %-9s %s\n" tool mode rc vacuity kernels summary
for tool in memcheck initcheck synccheck racecheck; do
  # cavity2d and case3d exercise the 2D and 3D field contracts across MANY
  # outer iterations, which is where a residency lifetime bug shows up;
  # gpusolver reaches the reduction kernels.
  for mode in cavity2d case3d gpusolver; do
    log="$E/cuda-diagnostics/${tool}-${mode}.log"
    { echo "# compute-sanitizer --tool $tool /tmp/pf_diag $mode"
      echo "# persistent-field residency path"
      echo "# lib: $(sha256sum "$BUILD/cuda/libcfdcuda.a" | cut -d' ' -f1)"; echo ""; } > "$log"
    "$CUDA/bin/compute-sanitizer" --tool "$tool" /tmp/pf_diag "$mode" >> "$log" 2>&1
    rc=$?; echo "# exit code: $rc" >> "$log"
    summary=$(grep -E "ERROR SUMMARY|RACECHECK SUMMARY" "$log" | tail -1)
    if grep -q "PRODUCTION GPU PATH EXERCISED" "$log"; then vac="non-vacuous"; else vac="VACUOUS"; fail=1; fi
    kernels=$(grep -oE "kernelLaunches=[0-9]+" "$log" | tail -1)
    case "$summary" in *"0 errors"*) ;; *"0 hazards"*) ;; *) fail=1 ;; esac
    [ $rc -ne 0 ] && fail=1
    printf "%-11s %-15s %-4s %-13s %-9s %s\n" "$tool" "$mode" "$rc" "$vac" \
      "${kernels#kernelLaunches=}" "$summary"
  done
done
RC[diagnostics]=$fail
echo "  -> rc=$fail"

echo ""
echo "##################### 2. CPU backend (CUDA disabled) #####################"
bash "$ROOT/results/gpu-disc-001/full-regression/tools/cpu_only.sh" > "$E/cpu-backend/run.log" 2>&1
RC[cpu]=$?
grep -E "rc=|tests passed|tests failed|nvcc invocations|does not link" "$E/cpu-backend/run.log" \
  | sed 's/^/  /'
echo "  -> rc=${RC[cpu]}"

echo ""
echo "##################### 3. performance 160/320/640 #####################"
BIN=/tmp/pf_perf bash "$ROOT/results/gpu-disc-001/performance/tools/build_and_run.sh" cavity \
  > "$E/performance/cavity.log" 2>&1
RC[perf]=$?
grep -E "^  (cpu|gpu-pipe|gpu-disc|disc-only) |BITWISE|PERFORMANCE BENCHMARK" \
  "$E/performance/cavity.log" | tail -30 | sed 's/^/  /'
echo "  -> rc=${RC[perf]}"

echo ""
echo "##################### 4. full repository regression #####################"
ninja -C "$BUILD" > /dev/null 2>&1
ctest --test-dir "$BUILD" --output-on-failure > "$E/regression/ctest.log" 2>&1
RC[regression]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/regression/ctest.log" | sed 's/^/  /'
ninja -C "$BUILD" 2>&1 | tail -1 | sed 's/^/  freshness after ctest: /'
echo "  -> rc=${RC[regression]}"

echo ""
echo "############################## SUMMARY ##############################"
bad=0
for k in diagnostics cpu perf regression; do
  printf "  %-12s rc=%s  %s\n" "$k" "${RC[$k]}" \
    "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
[ $bad -eq 0 ] && echo "GPU-PIPE-001 PERSISTENT FIELDS, FINAL PHASES: ALL PASS" \
               || echo "GPU-PIPE-001 PERSISTENT FIELDS, FINAL PHASES: FAILURES PRESENT"
exit $bad

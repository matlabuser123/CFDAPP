#!/usr/bin/env bash
# GPU-PIPE-001 GPU-resident pressure solve -- everything after the negative
# controls.
#
#   1. CUDA diagnostics on the resident path. The gpusolver mode is the one
#      that reaches it (both backends GPU, no fallback, no residency mirror);
#      the log now prints gpuDisc=2 there, so "the sanitizer actually examined
#      the resident kernels" is visible rather than inferred.
#   2. CPU backend, from a CUDA-disabled build -- the stub gained four methods
#      and GPU-DISC-001R showed that file is compiled by nothing else.
#   3. performance 20^2..640^2, against the GPU-DISC-001Q qualified baseline.
#   4. full repository regression.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/gpu-resident-pressure-solve
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
declare -A RC

ninja -C "$BUILD" > /dev/null 2>&1

echo "##################### 0. known technical debt, unchanged #####################"
# The recorded GPU BiCGSTAB restart asymmetry (TODO.md, baseline 548401a). The
# reproducer's gpu-disc arm runs the pressure solve DEVICE-RESIDENT now, so this
# is where a resident path that quietly changed breakdown behaviour would show.
# Not authorized to fix -- required to be UNCHANGED.
/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -o /tmp/rps_debt \
  "$ROOT/results/gpu-disc-001/full-regression/tools/known_debt_probe.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
/tmp/rps_debt > "$E/regression/known-debt.log" 2>&1
RC[debt]=$?
grep -E "KNOWN DEBT|status|iterations|breakdown" "$E/regression/known-debt.log" | tail -12 | sed 's/^/  /'
echo "  -> rc=${RC[debt]}"

echo ""
echo "##################### 1. CUDA diagnostics on the resident path #####################"
/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -g -std=c++20 -o /tmp/rps_diag \
  "$ROOT/results/gpu-disc-001/cuda-diagnostics/tools/diagnostic_workload.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
fail=0
printf "%-11s %-11s %-4s %-13s %-9s %-9s %s\n" tool mode rc vacuity kernels resident summary
for tool in memcheck initcheck synccheck racecheck; do
  for mode in cavity2d case3d gpusolver; do
    log="$E/cuda-diagnostics/${tool}-${mode}.log"
    { echo "# compute-sanitizer --tool $tool /tmp/rps_diag $mode"
      echo "# GPU-resident pressure solve"
      echo "# lib: $(sha256sum "$BUILD/cuda/libcfdcuda.a" | cut -d' ' -f1)"; echo ""; } > "$log"
    "$CUDA/bin/compute-sanitizer" --tool "$tool" /tmp/rps_diag "$mode" >> "$log" 2>&1
    rc=$?; echo "# exit code: $rc" >> "$log"
    summary=$(grep -E "ERROR SUMMARY|RACECHECK SUMMARY" "$log" | tail -1)
    if grep -q "PRODUCTION GPU PATH EXERCISED" "$log"; then vac="non-vacuous"; else vac="VACUOUS"; fail=1; fi
    kernels=$(grep -oE "kernelLaunches=[0-9]+" "$log" | tail -1)
    # gpuDisc=2 marks a workload whose pressure solve ran device-resident.
    if grep -q "gpuDisc=2" "$log"; then resident="resident"; else resident="-"; fi
    case "$summary" in *"0 errors"*) ;; *"0 hazards"*) ;; *) fail=1 ;; esac
    [ $rc -ne 0 ] && fail=1
    printf "%-11s %-11s %-4s %-13s %-9s %-9s %s\n" "$tool" "$mode" "$rc" "$vac" \
      "${kernels#kernelLaunches=}" "$resident" "$summary"
  done
done
# NON-VACUITY FOR THIS GATE SPECIFICALLY: at least one diagnosed workload must
# have taken the resident path, or the sanitizers never saw the new kernels.
if ! grep -lq "gpuDisc=2" "$E"/cuda-diagnostics/*-gpusolver.log 2>/dev/null; then
  echo "  FAIL no diagnosed workload took the resident path -- diagnostics are vacuous for this gate"
  fail=1
fi
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
echo "##################### 3. performance #####################"
BIN=/tmp/rps_perf bash "$ROOT/results/gpu-disc-001/performance/tools/build_and_run.sh" cavity \
  > "$E/performance/cavity.log" 2>&1
RC[perf]=$?
grep -E "^  (cpu|gpu-pipe|gpu-disc|disc-only) |BITWISE|PERFORMANCE BENCHMARK" \
  "$E/performance/cavity.log" | sed 's/^/  /'
echo "  -> rc=${RC[perf]}"

echo ""
echo "##################### 4. full repository regression #####################"
ctest --test-dir "$BUILD" --output-on-failure > "$E/regression/ctest.log" 2>&1
RC[regression]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/regression/ctest.log" | sed 's/^/  /'
ninja -C "$BUILD" 2>&1 | tail -1 | sed 's/^/  freshness after ctest: /'
echo "  -> rc=${RC[regression]}"

echo ""
echo "############################## SUMMARY ##############################"
bad=0
for k in debt diagnostics cpu perf regression; do
  printf "  %-12s rc=%s  %s\n" "$k" "${RC[$k]}" \
    "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
[ $bad -eq 0 ] && echo "RESIDENT PRESSURE SOLVE, FINAL PHASES: ALL PASS" \
               || echo "RESIDENT PRESSURE SOLVE, FINAL PHASES: FAILURES PRESENT"
exit $bad

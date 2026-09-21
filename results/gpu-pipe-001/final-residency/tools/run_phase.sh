#!/usr/bin/env bash
# GPU-PIPE-001 Final Residency -- one phase per invocation.
#
#   usage: run_phase.sh <phase> [args...]
#
# Phases run top to bottom and each STOPS at its first failed check, because the
# milestone's own rule is to stop at the first failed gate rather than continue
# to see whether later items would pass.
#
#   build       rebuild both build trees and record the binaries under test
#   roundtrip   the momentum host round trip, and the structural prerequisite
#   compare     the controlled resident-vs-host comparison (bitwise)
#   transfers   the resident SIMPLE loop transfer guard
#   equiv       CPU production vs GPU production
#   lifecycle   first/second solve, resize, new case, backend switch, recovery
#   dispatch    every configuration in which the resident loop must DECLINE
#   controls    the negative-control suite (mutate, detect, restore, re-pass)
#   diagnostics compute-sanitizer memcheck/initcheck/synccheck/racecheck
#   gates       the 15 GPU-DISC differential gates
#   perf        the 20^2..640^2 benchmark ladder
#   cpuonly     a from-scratch CUDA-disabled configure, build and test suite
#   regression  the full CTest suite on the production build
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/final-residency
T=$E/tools
FINAL=$ROOT/build/final
CUDABUILD=$ROOT/build/cuda
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

phase=${1:-}
shift || true

# Build one harness against a given build tree and run it.
build_run() {  # build_run <build-dir> <source> <output-log> [args...]
  local build=$1 src=$2 log=$3
  shift 3
  /usr/bin/c++ -I"$ROOT/include" -I"$build/generated/include" \
    -I"$build/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
    -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
    -o "/tmp/$(basename "$src" .cpp)" "$src" \
    -Wl,--start-group "$build/cuda/libcfdcuda.a" "$build/src/libcfdcore.a" -Wl,--end-group \
    -L"$CUDA/lib64" -lcudart > "$log.build" 2>&1
  if [ $? -ne 0 ]; then
    echo "BUILD FAILED: $src"
    tail -30 "$log.build"
    return 1
  fi
  { echo "# $(basename "$src") $*"
    echo "# date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "# libcfdcuda.a: $(sha256sum "$build/cuda/libcfdcuda.a" | cut -d' ' -f1)"
    echo "# libcfdcore.a: $(sha256sum "$build/src/libcfdcore.a" | cut -d' ' -f1)"
    echo ""; } > "$log"
  "/tmp/$(basename "$src" .cpp)" "$@" >> "$log" 2>&1
  local rc=$?
  echo "# exit code: $rc" >> "$log"
  return $rc
}

case "$phase" in

build)
  # BOTH trees. build/final carries the production CTest suite; build/cuda is
  # what the GPU-DISC gate harnesses and the performance benchmark link
  # against. A change built into only one of them would be measured by half
  # the evidence.
  for b in "$FINAL" "$CUDABUILD"; do
    echo "=== ninja -C $b ==="
    ninja -C "$b" 2>&1 | tail -3 || exit 1
  done
  {
    echo "# GPU-PIPE-001 final residency -- binaries under test"
    echo "date : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    for b in "$FINAL" "$CUDABUILD"; do
      sha256sum "$b/cuda/libcfdcuda.a" "$b/src/libcfdcore.a"
    done
    echo ""
    echo "# production sources under test"
    sha256sum src/pressure_velocity/SIMPLE.cpp \
      include/cfd/gpu/GpuSimpleDiscretization.hpp \
      include/cfd/gpu/GpuResidentSolve.hpp \
      include/cfd/gpu/DevicePersistentFields.hpp \
      include/cfd/pressure_velocity/SIMPLEResult.hpp \
      cuda/kernels/GpuSimpleDiscretizationCuda.cpp \
      cuda/kernels/GpuResidentSolveKernel.cu \
      cuda/kernels/GpuLinearSolverCuda.cpp \
      cuda/kernels/DevicePersistentFieldsKernel.cu \
      src/gpu/GpuSimpleDiscretization.cpp
  } | tee "$E/logs/build-identity.txt"
  ;;

roundtrip)
  build_run "$FINAL" "$T/momentum_roundtrip.cpp" "$E/simple-loop/momentum-roundtrip.log" "$@"
  rc=$?
  grep -E "PASS|FAIL|---|window|TOTAL|assemble\+rebuild|MOMENTUM ROUND TRIP" \
    "$E/simple-loop/momentum-roundtrip.log"
  exit $rc
  ;;

compare)
  build_run "$FINAL" "$T/resident_loop_comparison.cpp" "$E/simple-loop/comparison.log" "$@"
  rc=$?
  grep -E "PASS|FAIL|transfers/iteration|non-reduction|cases=|COMPARISON" \
    "$E/simple-loop/comparison.log"
  exit $rc
  ;;

transfers)
  build_run "$FINAL" "$T/loop_transfer_guard.cpp" "$E/transfers/after.log" "$@"
  rc=$?
  grep -E "PASS|FAIL|---|H2D|D2H|sync|resident|of which|GUARD" "$E/transfers/after.log"
  exit $rc
  ;;

equiv)
  build_run "$FINAL" "$T/production_equivalence.cpp" "$E/equivalence/production.log" "$@"
  rc=$?
  grep -E "PASS|FAIL|NOTE|cpu\[|history|mass imbalance|outer iterations|coverage|cases=|EQUIVALENCE" \
    "$E/equivalence/production.log"
  exit $rc
  ;;

lifecycle)
  build_run "$FINAL" "$T/lifecycle.cpp" "$E/lifecycle/probe.log" "$@"
  rc=$?
  grep -E "PASS|FAIL|---|authorities|allocations|the failing case|LIFECYCLE" "$E/lifecycle/probe.log"
  exit $rc
  ;;

dispatch)
  build_run "$FINAL" "$T/dispatch_probe.cpp" "$E/simple-loop/dispatch.log" "$@"
  rc=$?
  grep -E "PASS|FAIL|---|DISPATCH PROBE" "$E/simple-loop/dispatch.log"
  exit $rc
  ;;

controls)
  python3 "$T/negative_controls.py" "$@" > "$E/negative-controls/driver.log" 2>&1
  rc=$?
  tail -30 "$E/negative-controls/driver.log"
  exit $rc
  ;;

gates)
  bash "$ROOT/results/gpu-disc-001/full-regression/tools/gpu_gates.sh" \
    > "$E/regression/gpu-gates.log" 2>&1
  rc=$?
  grep -E "^(PASS|FAIL|BUILD-FAIL)|gates green" "$E/regression/gpu-gates.log"
  exit $rc
  ;;

diagnostics)
  # Built with -g so a sanitizer report names lines rather than addresses.
  /usr/bin/c++ -I"$ROOT/include" -I"$FINAL/generated/include" \
    -I"$FINAL/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
    -O2 -DNDEBUG -g -std=c++20 -o /tmp/resident_diag \
    "$T/resident_diagnostic_workload.cpp" \
    -Wl,--start-group "$FINAL/cuda/libcfdcuda.a" "$FINAL/src/libcfdcore.a" -Wl,--end-group \
    -L"$CUDA/lib64" -lcudart || exit 1
  fail=0
  printf "%-11s %-11s %-4s %-13s %-9s %-9s %s\n" tool mode rc vacuity kernels resident summary
  for tool in memcheck initcheck synccheck racecheck; do
    for mode in cavity2d case3d gpusolver lifecycle; do
      log="$E/cuda-diagnostics/${tool}-${mode}.log"
      { echo "# compute-sanitizer --tool $tool /tmp/resident_diag $mode"
        echo "# GPU-PIPE-001 final residency: the resident SIMPLE loop"
        echo "# lib: $(sha256sum "$FINAL/cuda/libcfdcuda.a" | cut -d' ' -f1)"; echo ""; } > "$log"
      "$CUDA/bin/compute-sanitizer" --tool "$tool" /tmp/resident_diag "$mode" >> "$log" 2>&1
      rc=$?; echo "# exit code: $rc" >> "$log"
      summary=$(grep -E "ERROR SUMMARY|RACECHECK SUMMARY" "$log" | tail -1)
      if grep -q "RESIDENT GPU PATH EXERCISED" "$log"; then vac="non-vacuous"; else vac="VACUOUS"; fail=1; fi
      kernels=$(grep -oE "kernelLaunches=[0-9]+" "$log" | tail -1)
      resident=$(grep -oE "residentWorkloads=[0-9]+" "$log" | tail -1)
      case "$summary" in *"0 errors"*) ;; *"0 hazards"*) ;; *) fail=1 ;; esac
      [ $rc -ne 0 ] && fail=1
      printf "%-11s %-11s %-4s %-13s %-9s %-9s %s\n" "$tool" "$mode" "$rc" "$vac" \
        "${kernels#kernelLaunches=}" "${resident#residentWorkloads=}" "$summary"
    done
  done
  # NON-VACUITY FOR THIS GATE: at least one diagnosed workload must have taken
  # the resident SIMPLE loop, or the sanitizers never saw the new kernels.
  if ! grep -q "resident=3" "$E"/cuda-diagnostics/memcheck-gpusolver.log 2>/dev/null; then
    echo "  FAIL no diagnosed workload took the resident SIMPLE loop -- diagnostics are vacuous"
    fail=1
  fi
  exit $fail
  ;;

perf)
  BIN=/tmp/final_resident_perf bash "$ROOT/results/gpu-disc-001/performance/tools/build_and_run.sh" \
    "${1:-cavity}" > "$E/performance/run-${1:-cavity}.log" 2>&1
  rc=$?
  grep -E "^(cavity|channel|inletoutlet)|^  (cpu|gpu-pipe|gpu-disc|disc-only) |BITWISE|PERFORMANCE BENCHMARK|wrote" \
    "$E/performance/run-${1:-cavity}.log"
  exit $rc
  ;;

cpuonly)
  bash "$ROOT/results/gpu-disc-001/full-regression/tools/cpu_only.sh" \
    > "$E/cpu-backend/run.log" 2>&1
  rc=$?
  grep -E "rc=|tests passed|tests failed|nvcc invocations|does not link|FAIL" "$E/cpu-backend/run.log"
  exit $rc
  ;;

regression)
  ctest --test-dir "$FINAL" --output-on-failure > "$E/regression/ctest.log" 2>&1
  rc=$?
  grep -E "tests passed|tests failed|Total Test time|\(Failed\)" "$E/regression/ctest.log"
  # Freshness AFTER the run: if ninja finds work to do now, the binaries just
  # tested were not built from the sources on disk.
  ninja -C "$FINAL" 2>&1 | tail -1 | sed 's/^/freshness after ctest: /'
  exit $rc
  ;;

*)
  echo "usage: run_phase.sh <build|dispatch|controls|roundtrip|compare|transfers|equiv|lifecycle|gates|diagnostics|perf|cpuonly|regression>"
  exit 2
  ;;
esac

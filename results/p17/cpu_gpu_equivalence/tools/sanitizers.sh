#!/usr/bin/env bash
# P17.10 -- the CUDA diagnostics, sections 26-27.
#
# EVERY RUN MUST PROVE IT EXECUTED. A sanitizer that fails to launch prints no errors either, which
# is indistinguishable from success in a log. So each run records the tool's own banner, the process
# exit status, the gtest pass line, and -- the control that matters -- how many of OUR kernels the
# tool actually observed. A run reporting ZERO tracked kernels is a FAILURE, not a pass.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/cpu_gpu_equivalence
BIN=build/qual-final/tests/unit/gpu/CFDGpuTests
SAN="$(command -v compute-sanitizer || echo /usr/local/cuda-12.9/bin/compute-sanitizer)"

echo "=== tool ==="
"$SAN" --version 2>&1 | head -3
echo "binary: $BIN"
test -x "$BIN" || { echo "FATAL: test binary missing"; exit 1; }

# racecheck and initcheck are far slower than memcheck, so they run a narrower but still
# representative filter: the residual gather (the only place a race could exist -- it has no
# atomics, which is exactly the claim under test) and one full trajectory.
run() {
  local tool="$1"; local filter="$2"; local log="$P17/logs/$3"
  echo
  echo "=== $tool  (filter: $filter) ==="
  OMP_NUM_THREADS=1 "$SAN" --tool "$tool" --error-exitcode 99 \
      "$BIN" --gtest_filter="$filter" > "$log" 2>&1
  local rc=$?
  echo "exit status: $rc"
  echo "banner:      $(grep -am1 'COMPUTE-SANITIZER' "$log")"
  echo "summary:     $(grep -aE '^=+ *(ERROR SUMMARY|RACECHECK SUMMARY|LEAK SUMMARY)' "$log" | head -2 | tr '\n' ' ')"
  echo "gtest:       $(grep -aE '^\[  (PASSED|FAILED)  \]' "$log" | tr '\n' ' ')"
  # The non-vacuity control: did the tool see OUR kernels at all?
  local seen
  seen=$(grep -acE 'recoverKernel|faceFluxKernel|gatherResidualKernel|localTimeStepKernel|minReduceKernel|applyUpdateKernel|fillKernel|minIndexReduceKernel' "$log")
  echo "our kernels named in the log: $seen"
  return $rc
}

run memcheck   'DensityBasedGpuEquivalence*'                      20_memcheck.log
run racecheck  'DensityBasedGpuEquivalence.B2_B4*:DensityBasedGpuEquivalence.C3_D1_D2*' 21_racecheck.log
run initcheck  'DensityBasedGpuEquivalence.B2_B4*:DensityBasedGpuEquivalence.C3_D1_D2*' 22_initcheck.log
run synccheck  'DensityBasedGpuEquivalence*'                      23_synccheck.log

echo
echo "############################################################################"
echo "# THE CONTROLS. Everything above reports 0 errors. These prove that 0 means"
echo "# 'nothing wrong' and not 'nothing instrumented'."
echo "############################################################################"

control() {
  local tool="$1"; local filter="$2"; local log="$P17/logs/$3"; local want="$4"
  echo
  echo "=== CONTROL: $tool on $filter ==="
  CFD_P17_10_SANITIZER_CONTROL=1 OMP_NUM_THREADS=1 "$SAN" --tool "$tool" "$BIN" --gtest_filter="$filter" > "$log" 2>&1
  echo "exit status: $?"
  echo "summary:     $(grep -aE '^=+ *(ERROR SUMMARY|RACECHECK SUMMARY)' "$log" | head -2 | tr '\n' ' ')"
  local hits
  hits=$(grep -ac "$want" "$log")
  echo "log lines naming '$want': $hits"
  if [ "$hits" -eq 0 ]; then
    echo "CONTROL FAILED: $tool did not report the deliberate fault, so its silence on the real"
    echo "                kernels proves nothing."
  else
    echo "CONTROL OK: $tool reported the deliberate fault."
  fi
}

control memcheck  'DensityBasedGpuSanitizerControl.N1*' 25_control_memcheck.log  sanitizerOobKernel
control racecheck 'DensityBasedGpuSanitizerControl.N2*' 26_control_racecheck.log sanitizerRaceKernel

echo
echo "=== done ==="

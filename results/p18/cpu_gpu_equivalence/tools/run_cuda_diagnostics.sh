#!/usr/bin/env bash
# P18.8 E13 -- the CUDA diagnostics harness.
#
# IT NEVER REPORTS A CLEAN RUN ALONE. A clean compute-sanitizer log is byte-for-byte what a
# sanitizer that instrumented nothing prints, so every clean result is paired with a LIVE CONTROL
# that the same tool, same binary, same machine must flag. If a control does not fire, the tool is
# reported BLIND and the harness exits non-zero -- it does not report a pass.
#
# THREE THINGS THE FIRST VERSION GOT WRONG, each caught by the harness failing closed:
#
#  1. It never checked that any TEST ACTUALLY RAN. "0 errors" is also what a sanitizer prints when
#     the gtest filter matched nothing. Every run now has its passed-test count compared against a
#     NATIVE run of the same filter, and a mismatch is NO RESULT.
#  2. It parsed only "ERROR SUMMARY". racecheck emits "RACECHECK SUMMARY: N hazards displayed",
#     so racecheck was reported NO RESULT. Both formats are parsed now.
#  3. It treated a missing summary as zero. A missing summary is now NO RESULT, never a pass.
#
# Usage:  run_cuda_diagnostics.sh <binary> <output-log>
set -u
set -o pipefail

BINARY="${1:?usage: run_cuda_diagnostics.sh <binary> <output-log>}"
LOG="${2:?usage: run_cuda_diagnostics.sh <binary> <output-log>}"
SANITIZER="${CFDAPP_COMPUTE_SANITIZER:-/usr/local/cuda-12.9/bin/compute-sanitizer}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Every production kernel, exercised in one process.
REAL_FILTER='P188GpuEquivalence.*:P188DeviceInvalidState.*:P188SanitizerControl.DISABLED_SANITIZER_SCOPE_EveryKernelIsLaunched'
OOB_FILTER='P188SanitizerControl.DISABLED_SANITIZER_CONTROL_OutOfBoundsDeviceAccess'
UNINIT_FILTER='P188SanitizerControl.DISABLED_SANITIZER_CONTROL_UninitialisedDeviceRead'

overall=0

# compute-sanitizer's own tally. memcheck/initcheck/synccheck print
#   "========= ERROR SUMMARY: N errors"
# racecheck prints
#   "========= RACECHECK SUMMARY: N hazards displayed (N errors, N warnings)"
# A run with neither is NO RESULT, printed as NOSUMMARY, never as 0.
error_count() {
  local file="$1" line
  line="$(grep -E '^=+ ERROR SUMMARY: ' "$file" | tail -1)"
  if [ -n "$line" ]; then
    echo "$line" | sed -E 's/.*ERROR SUMMARY: ([0-9]+).*/\1/'
    return
  fi
  line="$(grep -E '^=+ RACECHECK SUMMARY: ' "$file" | tail -1)"
  if [ -n "$line" ]; then
    # the errors figure inside the parentheses, not the "displayed" count
    echo "$line" | sed -E 's/.*\(([0-9]+) errors.*/\1/'
    return
  fi
  echo "NOSUMMARY"
}

# gtest's own passed count, so "it ran" is measured and not assumed.
passed_count() {
  local file="$1" line
  line="$(grep -E '^\[  PASSED  \] [0-9]+ test' "$file" | tail -1)"
  if [ -z "$line" ]; then
    echo "NORUN"
    return
  fi
  echo "$line" | sed -E 's/^\[  PASSED  \] ([0-9]+) test.*/\1/'
}

native_passed() {
  local filter="$1" out="$WORK/native_$2.log"
  "$BINARY" --gtest_also_run_disabled_tests --gtest_filter="$filter" >"$out" 2>&1
  passed_count "$out"
}

run_tool() {
  local tool="$1" filter="$2" out="$3"
  shift 3
  "$SANITIZER" --tool "$tool" "$@" "$BINARY" \
    --gtest_also_run_disabled_tests --gtest_filter="$filter" >"$out" 2>&1
  return 0
}

NATIVE_REAL="$(native_passed "$REAL_FILTER" real)"

{
  echo "P18.8 E13 -- CUDA DIAGNOSTICS"
  echo "date (WSL): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "binary: $BINARY"
  echo "sha256: $(sha256sum "$BINARY" | cut -d' ' -f1)"
  echo "sanitizer: $("$SANITIZER" --version 2>&1 | sed -n '3p')"
  echo "GPU: $(nvidia-smi --query-gpu=name,compute_cap,driver_version --format=csv,noheader)"
  echo "nvcc that built the kernels: $(grep -E '^CMAKE_CUDA_COMPILER:' build/qual-final/CMakeCache.txt | cut -d= -f2)"
  echo ""
  echo "KERNEL SCOPE: the filter below launches all five production kernels in one process --"
  echo "mixtureProperties, equationOfState, rateCoefficient, reactionSources, advanceReactor."
  echo "filter: $REAL_FILTER"
  echo "NATIVE run of that filter passes $NATIVE_REAL test(s). Every sanitizer run below must"
  echo "report the SAME count, or it did not actually execute the kernels and is NO RESULT."
  echo ""
} >"$LOG"

if [ "$NATIVE_REAL" = "NORUN" ] || [ "$NATIVE_REAL" -eq 0 ] 2>/dev/null; then
  echo "  E13: NOT ESTABLISHED -- the filter matches no tests natively, so nothing can be" >>"$LOG"
  echo "  instrumented. Fix the filter before reading any sanitizer result." >>"$LOG"
  exit 1
fi

# $1 tool  $2 real-log  $3 control-log or "-"  $4 control description
verdict() {
  local tool="$1" reallog="$2" controllog="$3" description="$4"
  local real control ran
  real="$(error_count "$reallog")"
  ran="$(passed_count "$reallog")"
  echo "=================== $tool ==================="
  echo "  production kernels : $real error(s),  $ran of $NATIVE_REAL test(s) passed"
  if [ "$ran" != "$NATIVE_REAL" ]; then
    echo "  VERDICT: NO RESULT -- $ran tests passed under the sanitizer against $NATIVE_REAL"
    echo "           natively. The kernels were not all executed, so a clean tally means nothing."
    overall=1
    echo ""
    return
  fi
  if [ "$controllog" != "-" ]; then
    control="$(error_count "$controllog")"
    echo "  LIVE CONTROL ($description) : $control error(s)"
    if [ "$control" = "NOSUMMARY" ] || { [ "$control" -eq 0 ] 2>/dev/null; }; then
      echo "  VERDICT: BLIND -- the tool did not flag a deliberate defect, so a clean result on the"
      echo "           production kernels proves nothing."
      overall=1
      echo ""
      return
    fi
  fi
  if [ "$real" = "NOSUMMARY" ]; then
    echo "  VERDICT: NO RESULT -- the run produced no summary line at all."
    overall=1
  elif [ "$real" -eq 0 ] 2>/dev/null; then
    if [ "$controllog" != "-" ]; then
      echo "  VERDICT: PASS -- 0 errors over all $ran tests, and the tool is proven live by the"
      echo "           control firing ($control error(s)) on the same binary and machine."
    else
      echo "  VERDICT: CLEAN, SCOPE-LIMITED -- 0 errors over all $ran tests. NO LIVE CONTROL"
      echo "           EXISTS for this tool here; see the scope note below."
    fi
  else
    echo "  VERDICT: FAIL -- $real error(s) in the production kernels."
    overall=1
  fi
  echo ""
}

# ------------------------------------------------------------------ memcheck
run_tool memcheck "$REAL_FILTER" "$WORK/memcheck_real.log" --leak-check full
run_tool memcheck "$OOB_FILTER" "$WORK/memcheck_control.log"
{
  verdict memcheck "$WORK/memcheck_real.log" "$WORK/memcheck_control.log" \
    "a deliberate out-of-bounds cudaMemcpy"
  echo "  --- control output, the lines that matter ---"
  grep -E 'ERROR SUMMARY|Invalid |out of bounds|=+ Program hit' "$WORK/memcheck_control.log" \
    | head -6 | sed 's/^/    /'
  echo ""
} >>"$LOG"

# ------------------------------------------------------------------ initcheck
run_tool initcheck "$REAL_FILTER" "$WORK/initcheck_real.log"
run_tool initcheck "$UNINIT_FILTER" "$WORK/initcheck_control.log"
{
  verdict initcheck "$WORK/initcheck_real.log" "$WORK/initcheck_control.log" \
    "a production kernel reading an uninitialised buffer"
  echo "  NOTE: that control routes through the REAL equationOfStateKernel and its real launcher"
  echo "  with one input deliberately left unwritten, so initcheck is proven live ON THESE"
  echo "  KERNELS rather than on a toy."
  echo "  --- control output, the lines that matter ---"
  grep -E 'ERROR SUMMARY|Uninitialized' "$WORK/initcheck_control.log" | head -4 | sed 's/^/    /'
  echo ""
} >>"$LOG"

# ------------------------------------------------- racecheck and synccheck
# Reported with their scope stated, because for THESE kernels a clean result is a weak claim and
# must not be dressed up as a strong one. There is no live control for either: manufacturing a
# shared-memory race would mean adding shared memory to a kernel that has none, which would be
# qualifying a different kernel than the one under test.
run_tool racecheck "$REAL_FILTER" "$WORK/racecheck_real.log"
{
  verdict racecheck "$WORK/racecheck_real.log" - -
  echo "  SCOPE: racecheck detects SHARED-MEMORY hazards. These kernels declare no shared memory"
  echo "  (measured below), so a clean result follows from that absence and is NOT evidence about"
  echo "  cross-thread interference in general."
  echo ""
} >>"$LOG"

run_tool synccheck "$REAL_FILTER" "$WORK/synccheck_real.log"
{
  verdict synccheck "$WORK/synccheck_real.log" - -
  echo "  SCOPE: synccheck detects divergent or illegal __syncthreads. These kernels contain none"
  echo "  (measured below), so a clean result follows from that absence."
  echo ""
  echo "  WHAT DOES COVER CROSS-THREAD BEHAVIOUR: one thread per cell, no atomics and no"
  echo "  cross-thread reads (structural, measured below), plus E10's bitwise equality across"
  echo "  block sizes 64/128/256/512 and across repeated runs."
  echo ""
} >>"$LOG"

# The structural claims the two scope notes rest on, MEASURED from the source rather than asserted.
{
  echo "=================== the structural claims, measured from the source ==================="
  for f in cuda/kernels/DeviceSpeciesKernel.cu cuda/kernels/DeviceSpeciesFmadControl.cu; do
    echo "  $f:"
    echo "    __shared__ declarations : $(grep -c '__shared__' "$f")"
    echo "    __syncthreads calls     : $(grep -c '__syncthreads' "$f")"
    echo "    atomic* calls           : $(grep -cE 'atomic(Add|Sub|Exch|Min|Max|CAS|And|Or|Xor)' "$f")"
    echo "    cooperative groups      : $(grep -c 'cooperative_groups' "$f")"
    echo "    warp shuffles           : $(grep -cE '__shfl' "$f")"
  done
  echo ""
  echo "=================== OVERALL ==================="
  if [ "$overall" -eq 0 ]; then
    echo "  E13: PASS -- memcheck and initcheck clean over all $NATIVE_REAL tests WITH their"
    echo "  controls firing; racecheck and synccheck clean with their scope stated."
  else
    echo "  E13: NOT ESTABLISHED -- see the verdicts above."
  fi
} >>"$LOG"

exit "$overall"

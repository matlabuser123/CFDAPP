#!/usr/bin/env bash
# GPU-PIPE-001 Final Residency -- Gate A assessment, read from the evidence.
#
# The resident-pressure-solve finalise run was started by a previous session and
# its console output went to that session's terminal, not to a file this one can
# read. So the verdict is reconstructed from the evidence FILES rather than from
# a printed summary -- which is the stronger way to do it anyway: every line
# below names the file it read.
#
# Gate A (from the authorization):
#   matrix resident; RHS resident; p' resident; persistent Krylov workspace
#   reused; avoidable full pressure-system transfers removed; numerical
#   equivalence passes; diagnostics pass.
#
# --------------------------------------------------------------------------
# THREE DEFECTS IN THE FIRST VERSION OF THIS SCRIPT, recorded rather than
# quietly fixed, because the third one made the whole instrument worthless:
#
#   1. `say "$(verdict $r)" ...` ran verdict inside a COMMAND SUBSTITUTION, so
#      its `fail=1` was set in a subshell and discarded. The script printed
#      "GATE A: PASS" while one of its own checks had printed FAIL. A gate
#      script that cannot fail is not a gate script.
#   2. `! grep -q "CHANGED"` on the known-debt log matched the substring inside
#      "UNCHANGED", so the correct result read as a failure.
#   3. `grep -c '^PASS'` counted zero because those logs indent their PASS
#      lines, so every count displayed as 0.
#
# Fixed below: the verdict is computed in the current shell, the known-debt
# check matches the exact verdict string, and the counts are unanchored. The
# self-check at the end proves the aggregation actually propagates.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/gpu-resident-pressure-solve
cd "$ROOT"
fail=0

# rc -> verdict string, aggregating into `fail` in THIS shell.
check() {  # check <rc> <what> <detail>
  local rc=$1 what=$2 detail=$3 v=PASS
  if [ "$rc" -ne 0 ]; then v=FAIL; fail=1; fi
  printf "  %-5s %-42s %s\n" "$v" "$what" "$detail"
}

echo "=== Gate A -- GPU-resident pressure solve ==="
echo ""
echo "--- A1..A4  residency: matrix, RHS, p', reused Krylov workspace ---"
grep -q "SOLVE TRANSFER GUARD: PASS" "$E/transfers/guard.log" 2>/dev/null; r=$?
check $r "transfers/guard.log" \
  "$(grep -E 'non-reduction D2H calls|allocations  ' "$E/transfers/guard.log" 2>/dev/null | tail -2 | tr -s ' ' | tr '\n' ';')"
lifePass=$(grep -c "PASS" "$E/lifecycle/probe.log" 2>/dev/null)
lifeFail=$(grep -c "FAIL" "$E/lifecycle/probe.log" 2>/dev/null)
[ "$lifeFail" -eq 0 ] && [ "$lifePass" -gt 0 ]; r=$?
check $r "lifecycle/probe.log" "$lifePass properties hold, $lifeFail FAIL"

echo ""
echo "--- A5  avoidable full pressure-system transfers removed ---"
grep -E "^    (assemble|solve|carry|non-reduction|allocations|Krylov)" "$E/transfers/guard.log" \
  2>/dev/null | tail -6 | sed 's/^/    /'

echo ""
echo "--- A6  numerical equivalence ---"
# This log has no per-check PASS lines -- it reports one verdict at the end and
# a block per case. So the check is the verdict string, and the case count is
# reported alongside it so a silently-empty run cannot read as a pass.
cmpCases=$(grep -cE "^=== cavity" "$E/comparison/comparison.log" 2>/dev/null)
grep -q "RESIDENCY COMPARISON: PASS (bitwise)" "$E/comparison/comparison.log" 2>/dev/null
r=$?
[ "$cmpCases" -gt 0 ] || r=1
check $r "comparison/comparison.log" \
  "$cmpCases cases compared bitwise, $(grep -c 'path taken        resident=yes  host=no' "$E/comparison/comparison.log" 2>/dev/null) with both arms asserted"
grep -q "REJECTION PROBE: PASS" "$E/comparison/rejection.log" 2>/dev/null; r=$?
check $r "comparison/rejection.log" "failure behaviour matches the host path"
grep -q "15/15" "$E/gpu-gates/gates.log" 2>/dev/null; r=$?
check $r "gpu-gates/gates.log" "$(tail -1 "$E/gpu-gates/gates.log" 2>/dev/null)"
grep -q "RESULT: PASS" "$E/negative-controls/driver.log" 2>/dev/null; r=$?
check $r "negative-controls/driver.log" \
  "$(grep -E 'detected observable' "$E/negative-controls/driver.log" 2>/dev/null)"

echo ""
echo "--- A7  CUDA diagnostics ---"
bad=0
for tool in memcheck initcheck synccheck racecheck; do
  for mode in cavity2d case3d gpusolver; do
    log="$E/cuda-diagnostics/${tool}-${mode}.log"
    s=$(grep -E "ERROR SUMMARY|RACECHECK SUMMARY" "$log" 2>/dev/null | tail -1)
    rc=$(grep -E "^# exit code:" "$log" 2>/dev/null | tail -1)
    if grep -q "PRODUCTION GPU PATH EXERCISED" "$log" 2>/dev/null; then vac=non-vacuous
    else vac=VACUOUS; bad=1; fi
    if grep -q "gpuDisc=2" "$log" 2>/dev/null; then res=resident; else res="-"; fi
    case "$s" in *"0 errors"*|*"0 hazards"*) ok=ok ;; *) ok=BAD; bad=1 ;; esac
    printf "    %-11s %-10s %-12s %-9s %-4s %s\n" "$tool" "$mode" "$vac" "$res" "$ok" \
      "$s  ${rc#\# }"
  done
done
check $bad "cuda-diagnostics/*.log" "12 runs, all non-vacuous"

echo ""
echo "--- supporting: CPU backend, performance, known debt, regression ---"
grep -q "0 tests failed out of 1932" "$E/cpu-backend/run.log" 2>/dev/null; r=$?
check $r "cpu-backend/run.log" \
  "$(grep -E 'tests passed|does not link|nvcc invocations' "$E/cpu-backend/run.log" 2>/dev/null | tr -s ' ' | tr '\n' ';')"
grep -q "PERFORMANCE BENCHMARK: all integrity checks passed" "$E/performance/cavity.log" 2>/dev/null
r=$?
check $r "performance/cavity.log" \
  "$(grep -c 'BITWISE differing=0' "$E/performance/cavity.log" 2>/dev/null) bitwise equivalence checks"
# The exact verdict string. "UNCHANGED" contains "CHANGED", so a substring test
# on the wrong one inverts the result -- that was defect 2 above.
grep -q "KNOWN DEBT: UNCHANGED" "$E/regression/known-debt.log" 2>/dev/null; r=$?
check $r "regression/known-debt.log" \
  "$(grep -E 'KNOWN DEBT' "$E/regression/known-debt.log" 2>/dev/null | tail -1)"
grep -q "0 tests failed" "$E/regression/ctest.log" 2>/dev/null; r=$?
check $r "regression/ctest.log" \
  "$(grep -E 'tests passed|Total Test time' "$E/regression/ctest.log" 2>/dev/null | tr '\n' ';')"

echo ""
echo "--- self-check: this script's aggregation can actually fail ---"
saved=$fail
check 1 "deliberate failing check (must read FAIL)" "proves the aggregator propagates"
if [ "$fail" -eq 1 ]; then echo "    aggregation works: fail flag was raised"; else
  echo "    BROKEN: a FAIL check did not raise the flag"; exit 2; fi
fail=$saved

echo ""
if [ $fail -eq 0 ]; then echo "GATE A: PASS"; else echo "GATE A: FAILURES PRESENT"; fi
exit $fail

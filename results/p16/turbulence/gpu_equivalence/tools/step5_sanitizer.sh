#!/usr/bin/env bash
# P16 Step 5, gate S5-1: compute-sanitizer over the GPU unit suite.
#
# Derived from results/gpu-pipe-001/phase2-reductions/tools/gate3_sanitizer.sh,
# which is preserved phase evidence and is NOT edited. Two changes:
#
#  1. The singular/plural fix. That script matches
#       'RACECHECK SUMMARY: [0-9]+ hazards displayed \([0-9]+ errors'
#     but with exactly ONE hazard the tool prints the singular form --
#       '1 hazard displayed (1 error, 0 warnings)'
#     -- which does not match. It then reports "could not parse" and fails
#     closed, so the old script is SAFE but mislabels the reason. Measured
#     during Step 5's instrument-validity control, which produced exactly one
#     hazard. Both forms are handled here.
#
#  2. Per-tool logs are kept under the Step-5 evidence directory rather than
#     /tmp, so a failure survives the session that found it.
#
# Fail-closed throughout: an unparsed summary is a FAILURE, never a default
# pass. A checker that misreads its own tool is worse than no checker.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CS=/usr/local/cuda-12.9/bin/compute-sanitizer
BIN=build/cuda/tests/unit/gpu/CFDGpuTests
OUT=results/p16/turbulence/gpu_equivalence/viscosity_path/sanitizer
mkdir -p "$OUT"
fail=0

FILTER="${1:-*}"
echo "binary : $BIN"
echo "filter : $FILTER"
echo "logs   : $OUT"
echo ""

for tool in memcheck initcheck racecheck synccheck; do
  log="$OUT/${tool}.log"
  "$CS" --tool "$tool" "$BIN" --gtest_filter="$FILTER" > "$log" 2>&1
  rc=$?
  if grep -q 'RACECHECK SUMMARY' "$log"; then
    # Handles 'N hazards displayed (N errors, ...)' AND the singular
    # '1 hazard displayed (1 error, ...)'.
    errs=$(grep -oE 'RACECHECK SUMMARY: [0-9]+ hazard(s)? displayed \([0-9]+ error' "$log" |
             grep -oE '\([0-9]+' | tr -d '(' | tail -1)
  else
    errs=$(grep -oE 'ERROR SUMMARY: [0-9]+' "$log" | grep -oE '[0-9]+$' | tail -1)
  fi
  if [ -z "${errs:-}" ]; then
    echo "  $tool: could not parse a sanitizer summary -- treating as FAILURE"
    errs=1
  fi
  tests=$(grep -oE '\[==========\] [0-9]+ tests' "$log" | grep -oE '[0-9]+' | tail -1)
  printf '  %-11s errors=%-6s tests=%-6s rc=%d\n' "$tool" "${errs:-?}" "${tests:-?}" "$rc"
  if [ "${errs:-1}" != "0" ] || [ "$rc" -ne 0 ]; then
    fail=1
    grep -E 'Invalid|race|Race|uninitial|Barrier' "$log" | head -5 | sed 's/^/        /'
  fi
done

echo ""
if [ "$fail" -eq 0 ]; then
  echo "S5-1: PASS (0 errors, all four tools)"
  echo "  CAVEAT, recorded in step5_gate.md: synccheck detection was never"
  echo "  demonstrated in this environment, so its clean result carries less"
  echo "  weight than memcheck/initcheck/racecheck, each of which was shown"
  echo "  to detect a planted defect first."
else
  echo "S5-1: FAIL"
fi
exit "$fail"

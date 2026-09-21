#!/usr/bin/env bash
# GPU-PIPE-001 Phase 2, Gate 3: compute-sanitizer over the GPU unit suite.
# racecheck matters most here -- the fused kernel keeps two shared-memory
# reduction trees in one block, and the device finalizer is a new kernel.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CS=/usr/local/cuda-12.9/bin/compute-sanitizer
BIN=build/cuda/tests/unit/gpu/CFDGpuTests
fail=0

for tool in memcheck initcheck synccheck racecheck; do
  log="/tmp/cs_${tool}.log"
  "$CS" --tool "$tool" "$BIN" > "$log" 2>&1
  rc=$?
  # memcheck/initcheck/synccheck print "ERROR SUMMARY: N errors"; racecheck
  # prints "RACECHECK SUMMARY: N hazards displayed (N errors, N warnings)".
  # Parsing only the first form left racecheck as "errors=?" and the gate
  # reported FAIL on a clean run -- a checker that misreads its own tool is
  # worse than no checker, so both forms are handled and an unparsed summary is
  # an explicit failure rather than a silent default.
  if grep -q 'RACECHECK SUMMARY' "$log"; then
    errs=$(grep -oE 'RACECHECK SUMMARY: [0-9]+ hazards displayed \([0-9]+ errors' "$log" |
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
    grep -E 'ERROR|Invalid|race|uninitial' "$log" | head -5 | sed 's/^/        /'
  fi
done

echo
[ "$fail" -eq 0 ] && echo "GATE 3: PASS (0 errors, all four tools)" || echo "GATE 3: FAIL"
exit "$fail"

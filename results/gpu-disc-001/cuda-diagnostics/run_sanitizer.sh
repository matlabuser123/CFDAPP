#!/usr/bin/env bash
# GPU-DISC-001 -- compute-sanitizer over a probe binary.
# usage: run_sanitizer.sh <binary> [label]
set -u
BIN="$1"
LABEL="${2:-$(basename "$BIN")}"
CS=/usr/local/cuda-12.9/bin/compute-sanitizer
fail=0

echo "=== compute-sanitizer: $LABEL ==="
for tool in memcheck initcheck synccheck racecheck; do
  log="/tmp/cs_disc_${tool}.log"
  "$CS" --tool "$tool" "$BIN" > "$log" 2>&1
  rc=$?
  if grep -q 'RACECHECK SUMMARY' "$log"; then
    errs=$(grep -oE 'RACECHECK SUMMARY: [0-9]+ hazards displayed \([0-9]+ errors' "$log" |
             grep -oE '\([0-9]+' | tr -d '(' | tail -1)
  else
    errs=$(grep -oE 'ERROR SUMMARY: [0-9]+' "$log" | grep -oE '[0-9]+$' | tail -1)
  fi
  if [ -z "${errs:-}" ]; then
    echo "  $tool: could not parse a summary -- treating as FAILURE"
    errs=1
  fi
  printf '  %-11s errors=%-4s rc=%d\n' "$tool" "$errs" "$rc"
  [ "$errs" != "0" ] && fail=1
done
echo
[ "$fail" -eq 0 ] && echo "SANITIZER: PASS (0 errors, all four tools)" || echo "SANITIZER: FAIL"
exit "$fail"

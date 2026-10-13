#!/usr/bin/env bash
# P13-002, control N4 -- the matrix must REFUSE a baseline that does not run.
#
# Every row the mutation matrix reports is a claim about what a mutation did.
# If the unmutated case were already rejected, every row would be measuring
# nothing and the sweep would still print "0 accepted" -- a perfect score for
# a vacuous run. The harness asserts the baseline RUNS before believing any
# row; this proves that assertion is real and not decorative.
#
# Input: tests/data/cases/bad_physics, a fixture that is invalid by design.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$ROOT/results/p13/final-qualification/2026-09-24/case_validation_logs
T=$ROOT/results/p13/final-qualification/2026-09-24/tools/case-validation
BIN=$ROOT/build/final/apps/cli/cfdapp
WORK=/tmp/p13_n4_case
cd "$ROOT"

rm -rf "$WORK"
cp -r tests/data/cases/bad_physics "$WORK"
rm -rf "$WORK/results"

log=$L/N4_control.log
{ echo "P13-002 control N4 -- the matrix must refuse a baseline that does not run"
  echo "date:  $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "input: tests/data/cases/bad_physics (invalid by design)"
  echo ""; } > "$log"

python3 "$T/mutation_matrix.py" "$WORK" "$BIN" >> "$log" 2>&1
rc=$?
{ echo ""; echo "harness exit: $rc"; } >> "$log"

if [ "$rc" -ne 0 ] && grep -q "ALREADY REJECTED" "$log"; then
  echo "N4: DETECTED -- the harness refused to report on a baseline that does not run" >> "$log"
  verdict=0
else
  echo "N4: UNDETECTED -- the harness reported rows for an already-invalid baseline," >> "$log"
  echo "    so a vacuous sweep could score 0 accepted" >> "$log"
  verdict=1
fi
cat "$log"
exit $verdict

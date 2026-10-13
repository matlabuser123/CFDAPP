#!/usr/bin/env bash
# P17.9 -- configure, build and run the shock-validation suite.
# A script file, never an inline `bash -lc`: inline invocation expands $PATH into Windows paths
# containing parentheses and the shell dies on the unquoted '('.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/shock_validation
TREE="${1:-build/qual-final}"
LOG="${2:-$P17/logs/03_shock.log}"
FILTER="${3:-*}"

cmake -S . -B "$TREE" > "$P17/logs/03_configure.log" 2>&1
echo "configure exit: $?"
cmake --build "$TREE" --target CFDShockValidationTests -j 16 > "$P17/logs/03_build.log" 2>&1
rc=$?
echo "build exit: $rc"
grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/03_build.log" | head -20
if [ "$rc" -ne 0 ]; then exit "$rc"; fi

OMP_NUM_THREADS=1 "$TREE/tests/integration/compressible/CFDShockValidationTests" \
  --gtest_filter="$FILTER" > "$LOG" 2>&1
echo "run exit: $?"
tail -6 "$LOG"

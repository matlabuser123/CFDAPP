#!/usr/bin/env bash
# P17.8 -- rebuild the focused targets and run the focused suite.
# A script file, not an inline bash -lc: inline invocation mangles $PATH and $? (see
# the WSL inline-command quoting hazard recorded in P17.7).
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/density_based_solver
TREE="${1:-build/qual-final}"
LOG="${2:-$P17/logs/05_focused_rerun.log}"
FILTER="${3:-Hllc|DensityBased}"

echo "=== tree: $TREE   filter: $FILTER"
cmake --build "$TREE" --target CFDCompressibleTests CFDGpuTests -j 16 > "$P17/logs/05_build.log" 2>&1
echo "build exit: $?"
grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/05_build.log" | head -8

OMP_NUM_THREADS=1 ctest --test-dir "$TREE" -j 8 -R "$FILTER" --output-on-failure --timeout 7200 > "$LOG" 2>&1
echo "ctest exit: $?"
grep -aE 'tests passed|tests failed' "$LOG" | tail -1 | sed 's/^/  /'
grep -aE '\(Failed\)' "$LOG" | sed 's/^/  FAILED: /' | head -12

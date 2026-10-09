#!/usr/bin/env bash
# Rebuild and run the P17.7 + P17.8 compressible suites. A script file, because inline
# `bash -lc` mangles $PATH into Windows paths containing parentheses.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/density_based_solver
LOG="${1:-$P17/logs/03_suite.log}"

cmake --build build/qual-final --target CFDCompressibleTests CFDGpuTests -j 16 \
  > "$P17/logs/03_build.log" 2>&1
echo "build exit: $?"
grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/03_build.log" | head -8

OMP_NUM_THREADS=1 ctest --test-dir build/qual-final -j 8 -R 'Hllc|DensityBased' \
  --output-on-failure > "$LOG" 2>&1
echo "ctest exit: $?"
grep -aE 'tests passed|tests failed' "$LOG" | tail -1 | sed 's/^/  /'
echo "  --- any failures ---"
grep -aE '\(Failed\)' "$LOG" | sed 's/^/  /' | head -12

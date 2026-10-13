#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/density_based_solver
TREE="${1:-build/qual-final}"
LOG="${2:-$P17/logs/09_canonical.log}"
cmake --build "$TREE" --target CFDDensityBasedCanonicalTests -j 16 > "$P17/logs/09_build.log" 2>&1
echo "build exit: $?"
grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/09_build.log" | head -12
OMP_NUM_THREADS=1 "$TREE/tests/integration/compressible/CFDDensityBasedCanonicalTests" > "$LOG" 2>&1
echo "run exit: $?"
tail -6 "$LOG"

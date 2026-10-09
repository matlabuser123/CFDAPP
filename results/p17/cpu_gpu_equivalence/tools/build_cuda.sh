#!/usr/bin/env bash
# P17.10 -- build the cfdcuda library alone, to iterate on the new kernel quickly.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/cpu_gpu_equivalence
TREE="${1:-build/qual-final}"
cmake -S . -B "$TREE" > "$P17/logs/03_configure.log" 2>&1
echo "configure exit: $?"
cmake --build "$TREE" --target cfdcuda -j 16 > "$P17/logs/03_build_cuda.log" 2>&1
rc=$?
echo "build exit: $rc"
grep -aE '(^|[^a-zA-Z])error(:| #)' "$P17/logs/03_build_cuda.log" | head -40
exit "$rc"

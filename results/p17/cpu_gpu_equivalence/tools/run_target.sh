#!/usr/bin/env bash
# P17.10 -- build and run one gtest target.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/cpu_gpu_equivalence
# The filter arrives via GTEST_FILTER, never as an argv glob: wsl.exe hands arguments to a
# login shell that expands them against the Windows cwd and fails on no match.
TREE="$1"; TARGET="$2"; BIN="$3"; LOG="$4"
FILTER="${GTEST_FILTER:-*}"
unset GTEST_FILTER
cmake -S . -B "$TREE" > "$P17/logs/cfg_$TARGET.log" 2>&1
echo "configure exit: $?"
cmake --build "$TREE" --target "$TARGET" -j 16 > "$P17/logs/build_$TARGET.log" 2>&1
rc=$?
echo "build exit: $rc"
grep -aE '(^|[^a-zA-Z])error(:| #)' "$P17/logs/build_$TARGET.log" | head -30
if [ "$rc" -ne 0 ]; then exit "$rc"; fi
OMP_NUM_THREADS=1 "$TREE/$BIN" --gtest_filter="$FILTER" > "$LOG" 2>&1
echo "run exit: $?"
tail -8 "$LOG"

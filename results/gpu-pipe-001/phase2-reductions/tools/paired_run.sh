#!/usr/bin/env bash
# GPU-PIPE-001 Phase 2 -- paired before/after measurement.
#
# Phase 1 measured ~10% cross-session variation, so BEFORE and AFTER are run
# ALTERNATING inside one session, repeated, and compared by median. Comparing a
# fresh AFTER against a historical BEFORE would not be evidence.
#
# BEFORE = HEAD (a git worktree, built outside the working tree so the AFTER
#          sources are never disturbed)
# AFTER  = the current working tree
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

BEFORE_BIN=/tmp/paired_before
AFTER_BIN=/tmp/paired_after
OUT=results/gpu-pipe-001/phase2-reductions/paired-benchmarks/raw.txt
mkdir -p "$(dirname "$OUT")"
: > "$OUT"

REPEATS=${REPEATS:-3}

# One untimed warm-up per side: the CUDA context/first-kernel-load cost is real
# but one-off, and whichever side runs first would otherwise absorb all of it
# (the 40x40 sanity run showed exactly that -- 1.14 s vs 0.44 s on first use).
echo "warm-up (discarded)"
"$BEFORE_BIN" 160 2 gpu > /dev/null 2>&1
"$AFTER_BIN"  160 2 gpu > /dev/null 2>&1

# edge:outer -- budgets follow the Phase-1 benchmark's own per-grid table.
for spec in 160:8 320:6 640:4; do
  edge=${spec%%:*}
  outer=${spec##*:}
  for r in $(seq 1 "$REPEATS"); do
    for side in before after; do
      bin=$BEFORE_BIN
      [ "$side" = after ] && bin=$AFTER_BIN
      line=$("$bin" "$edge" "$outer" gpu 2>&1 | grep '^RESULT')
      echo "side=$side repeat=$r $line" | tee -a "$OUT"
    done
  done
done

echo
echo "raw results -> $OUT"

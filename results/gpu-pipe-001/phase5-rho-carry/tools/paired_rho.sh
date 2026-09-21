#!/usr/bin/env bash
# GPU-PIPE-001 rho-carry -- paired BEFORE/AFTER, same session, alternating.
#
# BEFORE = /tmp/paired_before_rho, the probe built against the library as it
#          stood after Phase 3 (5 reduction groups per iteration).
# AFTER  = /tmp/paired_after, rebuilt against the rho-carry library (4 groups).
# Both are statically linked, so neither is affected by later rebuilds.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
OUT=results/gpu-pipe-001/phase5-rho-carry/paired_raw.txt
: > "$OUT"
REPEATS=${REPEATS:-3}

echo "warm-up (discarded)"
/tmp/paired_before_rho 160 2 gpu > /dev/null 2>&1
/tmp/paired_after      160 2 gpu > /dev/null 2>&1

for spec in 160:8 320:6 640:4; do
  edge=${spec%%:*}; outer=${spec##*:}
  for r in $(seq 1 "$REPEATS"); do
    for side in before after; do
      bin=/tmp/paired_before_rho
      [ "$side" = after ] && bin=/tmp/paired_after
      line=$("$bin" "$edge" "$outer" gpu 2>&1 | grep '^RESULT')
      echo "side=$side repeat=$r $line" >> "$OUT"
    done
  done
  echo "  ${edge}^2 done"
done
echo "raw -> $OUT"

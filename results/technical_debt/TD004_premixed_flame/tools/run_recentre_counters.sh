#!/bin/bash
set -u
ITEM=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/technical_debt/TD004_premixed_flame
OUT=$ITEM/logs/${1:-A2_01_recentre_counters}.log
if [ -s "$OUT" ]; then
  echo "*** REFUSED: $OUT exists and is non-empty (CLAUDE.md section 1.5)."
  exit 1
fi
setsid nohup bash "$ITEM/tools/do_recentre_counters.sh" > "$OUT" 2>&1 < /dev/null &
echo "detached pid $! -> $(basename "$OUT")"
sleep 3
tail -2 "$OUT"

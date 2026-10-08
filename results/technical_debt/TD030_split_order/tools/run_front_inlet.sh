#!/bin/bash
# TD-030 AUDIT -- launch do_front_inlet.sh detached, so a reaped front-end or a WSL restart cannot
# truncate it (which is what cost TD-004's A1_02).
set -u
ITEM=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/technical_debt/TD030_split_order
OUT=$ITEM/logs/${1:-03_front_inlet}.log
if [ -s "$OUT" ]; then
  echo "*** REFUSED: $OUT exists and is non-empty. Pick another name rather than overwriting a"
  echo "    recorded measurement (CLAUDE.md section 1.5)."
  exit 1
fi
setsid nohup bash "$ITEM/tools/do_front_inlet.sh" > "$OUT" 2>&1 < /dev/null &
echo "detached pid $! -> $(basename "$OUT")"
sleep 3
tail -3 "$OUT"

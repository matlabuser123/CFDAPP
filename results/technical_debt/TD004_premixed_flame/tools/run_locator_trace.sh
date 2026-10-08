#!/bin/bash
set -u
ITEM=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/technical_debt/TD004_premixed_flame
OUT=$ITEM/logs/${1:-A2_03_locator_trace}.log
if [ -s "$OUT" ]; then
  echo "*** REFUSED: $OUT exists and is non-empty (CLAUDE.md section 1.5)."
  exit 1
fi
BUSY=$(ps -eo comm --sort=-pcpu | sed -n '2p')
case "$BUSY" in
  td004_*|td030_*|td037_*|CFDSpeciesTests|cc1plus)
    echo "*** REFUSED: '$BUSY' is already running. An evidence run gets the machine"
    echo "    (CLAUDE.md hardware policy), and overlapping one cost a void dry-run already."
    exit 1 ;;
esac
setsid nohup bash "$ITEM/tools/do_locator_trace.sh" > "$OUT" 2>&1 < /dev/null &
echo "detached pid $! -> $(basename "$OUT")"
sleep 3
tail -2 "$OUT"

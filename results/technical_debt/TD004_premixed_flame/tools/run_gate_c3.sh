#!/bin/bash
set -u
ITEM=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/technical_debt/TD004_premixed_flame
OUT=$ITEM/logs/${1:-A2_06_gate_from_C3}.log
[ -s "$OUT" ] && { echo "*** REFUSED: $OUT exists and is non-empty."; exit 1; }
BUSY=$(ps -eo comm --sort=-pcpu | sed -n '2p')
case "$BUSY" in
  td00*|td03*|td04*|CFDSpeciesTests|cc1plus)
    echo "*** REFUSED: '$BUSY' is running. An evidence run gets the machine."; exit 1 ;;
esac
setsid nohup bash "$ITEM/tools/do_gate_c3.sh" > "$OUT" 2>&1 < /dev/null &
echo "detached pid $! -> $(basename "$OUT")"
sleep 4; tail -4 "$OUT"

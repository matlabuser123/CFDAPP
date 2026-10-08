#!/bin/bash
set -u
ITEM=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/technical_debt/TD040_recentre_conservation
OUT=$ITEM/logs/${1:-02_prefix_control}.log
[ -s "$OUT" ] && { echo "*** REFUSED: $OUT exists and is non-empty."; exit 1; }
BUSY=$(ps -eo comm --sort=-pcpu | sed -n '2p')
case "$BUSY" in
  td00*|td03*|td04*|CFDSpeciesTests|cc1plus)
    echo "*** REFUSED: '$BUSY' is running. An evidence run gets the machine."; exit 1 ;;
esac
setsid nohup bash "$ITEM/tools/do_prefix_control.sh" > "$OUT" 2>&1 < /dev/null &
echo "detached pid $! -> $(basename "$OUT")"
sleep 4; tail -3 "$OUT"

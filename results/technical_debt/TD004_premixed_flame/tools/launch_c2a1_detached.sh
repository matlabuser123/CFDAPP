#!/bin/bash
# Launch the FROZEN C2-A1 probe detached inside WSL, so the work survives the wsl.exe front-end
# being reaped -- which is what truncated A1_02. A running process also keeps the VM from idling
# out. The probe and the gate are unchanged; only how the run is started differs.
#
# usage: launch_c2a1_detached.sh <log-basename>
set -u
ITEM=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/technical_debt/TD004_premixed_flame
LOG=${1:-A1_03_c2a1_remeasure}
if [ -s "$ITEM/logs/$LOG.log" ]; then
  echo "*** REFUSED: $LOG.log already exists and is non-empty. Pick another name rather than"
  echo "    overwriting a recorded measurement (CLAUDE.md section 1.5)."
  exit 1
fi
setsid nohup bash "$ITEM/tools/run_c2a1_probe.sh" > "$ITEM/logs/$LOG.log" 2>&1 < /dev/null &
echo "detached pid $! -> logs/$LOG.log"
sleep 2
ps -eo pid,etimes,comm --sort=-etimes | grep -E "bash" | head -3

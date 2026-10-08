#!/bin/bash
# TD-030 AUDIT step 1 -- re-run TD-005's FUEL BUDGET probe, unchanged, on the fixed tree.
#
# That probe produced the 107.892 % reaction-integral excess recorded in TD-005 dry_run.md §O, and
# it validates itself first by recomputing the residual and comparing against the solver's reported
# value. Its fixture pins `timeStep` explicitly (D = 0.833), so re-running it unchanged reproduces
# §O's conditions exactly and isolates what TD-032's and TD-037's fixes did to the budget.
#
# The probe is NOT edited. Its output goes to TD-030's own evidence directory, because TD-005's
# logs are that item's record (CLAUDE.md §4) -- and because a hardcoded-path probe writing into
# someone else's phase directory is a hazard this repo has already been bitten by.
set -u
TD005=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/technical_debt/TD005_flame_speed_extraction
OUT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/technical_debt/TD030_split_order/logs/01_budget_remeasure.log
if [ -s "$OUT" ]; then
  echo "*** REFUSED: $OUT already exists and is non-empty."
  exit 1
fi
setsid nohup bash "$TD005/tools/run_fuel_budget_probe.sh" > "$OUT" 2>&1 < /dev/null &
echo "detached pid $! -> TD030_split_order/logs/01_budget_remeasure.log"
sleep 2
ps -eo pid,etimes,comm --sort=-etimes | grep bash | head -3

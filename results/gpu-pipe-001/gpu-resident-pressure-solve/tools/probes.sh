#!/usr/bin/env bash
# GPU-PIPE-001 GPU-resident pressure solve -- the gate's own probes.
#
#   1. transfer guard      is anything still crossing per pressure solve?
#   2. rejection probe     does the resident path refuse what the host refuses?
#   3. lifecycle probe     repeated solves, re-prepare both ways, restart
#
# Run before the negative controls: a control suite whose detectors do not pass
# on the clean tree proves nothing about the mutations.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/gpu-resident-pressure-solve
BR=$ROOT/results/gpu-pipe-001/persistent-fields/tools/build_and_run.sh
cd "$ROOT"
declare -A RC

echo "##################### 1. transfer guard (per pressure solve) #####################"
bash "$BR" "$E/tools/solve_transfer_guard.cpp" > "$E/transfers/guard.log" 2>&1
RC[guard]=$?
grep -E "===|window|assemble|solve |carry|non-reduction|allocations|Krylov|FAIL|GUARD:" "$E/transfers/guard.log" \
  | sed 's/^/  /'
echo "  -> rc=${RC[guard]}"

echo ""
echo "##################### 2. rejection behaviour #####################"
bash "$BR" "$E/tools/rejection_probe.cpp" > "$E/comparison/rejection.log" 2>&1
RC[rejection]=$?
grep -E "host |FAIL|REJECTION PROBE|--" "$E/comparison/rejection.log" | sed 's/^/  /'
echo "  -> rc=${RC[rejection]}"

echo ""
echo "##################### 3. lifecycle and re-entry #####################"
bash "$BR" "$E/tools/lifecycle_probe.cpp" > "$E/lifecycle/probe.log" 2>&1
RC[lifecycle]=$?
grep -E "PASS|FAIL|--|workspace|allocations after|reused|before " "$E/lifecycle/probe.log" \
  | sed 's/^/  /'
echo "  -> rc=${RC[lifecycle]}"

echo ""
echo "############################## SUMMARY ##############################"
bad=0
for k in guard rejection lifecycle; do
  printf "  %-11s rc=%s  %s\n" "$k" "${RC[$k]}" \
    "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
[ $bad -eq 0 ] && echo "RESIDENT PRESSURE SOLVE PROBES: ALL PASS" \
               || echo "RESIDENT PRESSURE SOLVE PROBES: FAILURES PRESENT"
exit $bad

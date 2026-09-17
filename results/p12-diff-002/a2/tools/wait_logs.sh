#!/usr/bin/env bash
# Wait until both long A2 runs have written their terminating line, then print their tails.
P=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-diff-002
CASES=$P/logs/17_A2_5_cases_after.log
W7=$P/a2/logs/10_A2_7_focused.log
for i in $(seq 1 110); do
  a=0; b=0
  grep -q '^exit' "$CASES" && a=1
  grep -q 'CFDCaseIntegrationTests:' "$W7" && b=1
  if [ "$a" = 1 ] && [ "$b" = 1 ]; then echo "BOTH DONE after $((i*5))s"; break; fi
  sleep 5
done
echo "===== A2-5 cases ====="
tail -12 "$CASES"
echo "===== A2-7 focused ====="
tail -3 "$W7"

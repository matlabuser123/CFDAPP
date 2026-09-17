#!/usr/bin/env bash
L=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-diff-002/a3/logs/05_A3_5_focused.log
for i in $(seq 1 100); do
  if grep -q 'CFDTurbulenceTests:' "$L"; then echo "DONE"; break; fi
  sleep 6
done
tail -24 "$L"

#!/usr/bin/env bash
# GPU-DISC-001Q -- the non-cavity benchmark families, run sequentially.
#
# Sequential is not a convenience: two GPU benchmarks in flight would contend
# for the device and every timing in both would be wrong.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$ROOT/results/gpu-disc-001/performance
cd "$ROOT"

echo "########## cavity analysis (already measured) ##########"
python3 "$P/tools/analyse.py" cavity > "$P/scaling/cavity_analysis.txt" 2>&1
echo "  rc=$? -> scaling/cavity_analysis.txt"

for m in inletoutlet 3d schemes; do
  echo ""
  echo "########## $m ##########"
  bash "$P/tools/build_and_run.sh" "$m" > "$P/raw/run-$m.log" 2>&1
  rc=$?
  echo "  rc=$rc"
  tail -4 "$P/raw/run-$m.log"
  if [ $rc -eq 0 ]; then
    python3 "$P/tools/analyse.py" "$m" > "$P/raw/analysis-$m.txt" 2>&1
    echo "  analysed -> raw/analysis-$m.txt"
  fi
done

echo ""
echo "REMAINING BENCHMARK FAMILIES: done"

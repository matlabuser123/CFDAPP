#!/usr/bin/env bash
# GPU-DISC-001Q -- every benchmark family, sequentially, with analysis.
#
# Sequential is not a convenience: two GPU benchmarks in flight would contend
# for the device and every timing in both would be wrong.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$ROOT/results/gpu-disc-001/performance
cd "$ROOT"

rc_all=0
for m in cavity inletoutlet 3d schemes; do
  echo ""
  echo "########## $m ##########"
  bash "$P/tools/build_and_run.sh" "$m" > "$P/raw/run-$m.log" 2>&1
  rc=$?
  echo "  rc=$rc"
  grep -E "BITWISE|PERFORMANCE BENCHMARK" "$P/raw/run-$m.log" | tail -8
  [ $rc -ne 0 ] && rc_all=1
  python3 "$P/tools/analyse.py" "$m" > "$P/raw/analysis-$m.txt" 2>&1
done

cp "$P/raw/analysis-cavity.txt" "$P/scaling/cavity_analysis.txt" 2>/dev/null

echo ""
if [ $rc_all -eq 0 ]; then
  echo "ALL BENCHMARK FAMILIES: integrity checks passed"
else
  echo "ALL BENCHMARK FAMILIES: INTEGRITY FAILURES -- see the per-family logs"
fi
exit $rc_all

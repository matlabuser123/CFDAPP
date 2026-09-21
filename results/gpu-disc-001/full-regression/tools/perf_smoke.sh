#!/usr/bin/env bash
# GPU-DISC-001R Phase L -- performance sanity, not a re-qualification.
#
# GPU-DISC-001Q already qualified the full matrix. This only checks the FINAL
# clean build has not suffered a catastrophic regression, using the guard that
# gate built -- so the comparison is against recorded evidence with a recorded
# threshold, not against a number remembered from a paragraph.
#
# 160^2 and 320^2 only. 640^2 costs ~90 s per CPU repeat and adds nothing a
# sanity check needs.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$ROOT/results/gpu-disc-001/performance
EVID=$ROOT/results/gpu-disc-001/full-regression/performance-smoke
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
mkdir -p "$EVID"

echo "=== build the benchmark against the FINAL clean build ==="
/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 \
  -o /tmp/final_perf "$P/tools/performance_benchmark.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1

echo ""
echo "=== smoke: 160^2 and 320^2, all four arms ==="
/tmp/final_perf smoke > "$EVID/smoke.log" 2>&1
rc=$?
grep -E "^  (cpu|gpu-pipe|gpu-disc|disc-only) |BITWISE|PERFORMANCE BENCHMARK" "$EVID/smoke.log" \
  | sed 's/^/  /'
echo "  rc=$rc"

echo ""
echo "=== compare against the GPU-DISC-001Q qualified baseline ==="
echo "  (exact counters at 0%, timing at the 35% threshold that gate derived)"
python3 "$P/tools/regression_guard.py" check "$P/raw/runs-smoke.csv" \
  "$P/regression-guard/baseline.json" > "$EVID/guard.log" 2>&1
guardRc=$?
tail -20 "$EVID/guard.log" | sed 's/^/  /'
echo "  guard rc=$guardRc"

# The guard reports MISSING points because the smoke set is a subset of the
# baseline's 24. That is expected and is not a regression -- only genuine
# regressions and exact-counter changes on the points PRESENT matter here.
echo ""
echo "  NOTE: 'missing' points are the baseline grids this subset does not run."
echo "        Only TIMING REGRESSIONS and EXACT-COUNTER CHANGES are findings."
if grep -q "TIMING REGRESSIONS" "$EVID/guard.log"; then
  echo "  FINDING: timing regression reported -- see guard.log"
  exit 1
fi
if grep -q "EXACT-COUNTER CHANGES" "$EVID/guard.log"; then
  echo "  FINDING: exact-counter change reported -- see guard.log"
  exit 1
fi
echo "  no timing regression, no exact-counter change"

echo ""
[ $rc -eq 0 ] && echo "PERFORMANCE SMOKE: PASS" || echo "PERFORMANCE SMOKE: FAIL"
exit $rc

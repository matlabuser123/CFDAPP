#!/usr/bin/env bash
# GPU-DISC-001Q -- the short probes, after the benchmark families.
#   1. device-memory behaviour  (per-iteration creep, leak, creep across solves)
#   2. Nsight Systems profile   (DEVICE time per kernel -- the stage timers
#                                deliberately cannot give this)
#   3. CPU thread scaling       (proves the CPU baseline is not throttled)
#   4. regression-guard baseline
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$ROOT/results/gpu-disc-001/performance
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

echo "########## 1. device memory ##########"
/usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
  -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o /tmp/gpu_disc_mem "$P/tools/memory_probe.cpp" \
  -Wl,--start-group build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
/tmp/gpu_disc_mem > "$P/memory/memory_probe.log" 2>&1
echo "  rc=$?"
tail -22 "$P/memory/memory_probe.log"

echo ""
echo "########## 2. Nsight Systems profile ##########"
BIN=/tmp/gpu_disc_perf_profile bash "$P/tools/build_and_run.sh" profile \
  > "$P/profiling/build.log" 2>&1
echo "  build+run rc=$?"
tail -3 "$P/profiling/build.log"
bash "$P/tools/profile.sh" > "$P/profiling/profile.log" 2>&1
echo "  profile rc=$?"
tail -25 "$P/profiling/profile.log"

echo ""
echo "########## 3. CPU thread scaling ##########"
bash "$P/tools/cpu_threads.sh" > "$P/scaling/cpu_threads.log" 2>&1
echo "  rc=$?"
tail -14 "$P/scaling/cpu_threads.log"

echo ""
echo "########## 4. regression-guard baseline ##########"
python3 "$P/tools/regression_guard.py" record "$P/raw/runs-cavity.csv" \
  "$P/regression-guard/baseline.json"
echo ""
echo "  self-check: the baseline must pass against the data it was recorded from"
python3 "$P/tools/regression_guard.py" check "$P/raw/runs-cavity.csv" \
  "$P/regression-guard/baseline.json" | tail -4

echo ""
echo "PROBES: done"

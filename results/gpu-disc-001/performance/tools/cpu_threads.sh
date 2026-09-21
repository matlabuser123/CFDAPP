#!/usr/bin/env bash
# GPU-DISC-001Q -- is the CPU baseline fair?
#
# The brief forbids an artificially weak CPU configuration. src/ contains
# exactly ONE OpenMP region -- SparseMatrix.cpp:91, the CPU SpMV -- and it is
# bit-identical at any thread count (row-parallel, each row's inner sum summed
# serially in ascending column order). So thread count changes speed, not
# numbers, which makes this a legitimate extra data point rather than a
# different numerical problem.
#
# The PRIMARY comparison uses the default (OMP_NUM_THREADS unset -> nproc = 32),
# which is the project's production CPU configuration and the one the
# GPU-PIPE-001 baseline used. This records 1, 8 and 32 threads at the two
# largest grids -- where the SpMV dominates and a thread effect is actually
# visible -- so "the CPU was not throttled" is a measurement, not an assertion.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$ROOT/results/gpu-disc-001/performance
cd "$ROOT"

for threads in 1 8 32; do
  echo "########## OMP_NUM_THREADS=$threads ##########"
  OMP_NUM_THREADS=$threads BIN=/tmp/gpu_disc_perf_omp \
    bash "$P/tools/build_and_run.sh" cpuscale > "$P/scaling/cpu-threads-$threads.log" 2>&1
  echo "  rc=$?"
  mv "$P/raw/runs-cpuscale.csv" "$P/raw/runs-cpuscale-omp$threads.csv" 2>/dev/null
  grep -E "^ +cpu " "$P/scaling/cpu-threads-$threads.log"
done

echo ""
echo "=== summary: CPU wall seconds by thread count ==="
printf "%-10s %-12s %-12s\n" "threads" "320x320" "640x640"
for threads in 1 8 32; do
  f=$P/raw/runs-cpuscale-omp$threads.csv
  [ -f "$f" ] || continue
  a=$(awk -F, '$2=="320x320" && $5=="cpu"{print $10}' "$f" | sort -n | awk '{v[NR]=$1} END{print v[int((NR+1)/2)]}')
  b=$(awk -F, '$2=="640x640" && $5=="cpu"{print $10}' "$f" | sort -n | awk '{v[NR]=$1} END{print v[int((NR+1)/2)]}')
  printf "%-10s %-12s %-12s\n" "$threads" "$a" "$b"
done

echo ""
echo "CPU THREAD SCALING: done"

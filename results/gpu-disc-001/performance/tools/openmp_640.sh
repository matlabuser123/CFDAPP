#!/usr/bin/env bash
# GPU-DISC-001Q -- the headline 640^2 point against a TUNED CPU.
#
# The 320^2 sweep on the OpenMP build found 8 threads fastest (22.26 s) and 32
# threads SLOWER than serial (39.45 s, 67.5% spread -- oversubscription on a
# 16-core/32-thread laptop part). So the fair CPU denominator is the best
# configuration, not the default one, and the most-quoted number in this gate
# (640^2) has to be recomputed against it.
#
# 1 and 8 threads only: 32 was already shown to be counter-productive, and a
# 640^2 CPU run is ~90 s serial.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$ROOT/results/gpu-disc-001/performance
CUDA=/usr/local/cuda-12.9
BUILD=$ROOT/build/omp
cd "$ROOT"

/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -fopenmp \
  -o /tmp/gpu_disc_perf_omp_build "$P/tools/performance_benchmark.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart -fopenmp || exit 1

for threads in 1 8; do
  OMP_NUM_THREADS=$threads /tmp/gpu_disc_perf_omp_build cpuscale 640 \
    > "$P/scaling/openmp/cpuscale640-$threads.log" 2>&1
  mv "$P/raw/runs-cpuscale.csv" "$P/scaling/openmp/runs-cpuscale640-omp$threads.csv" 2>/dev/null
  printf "  OMP_NUM_THREADS=%-3s " "$threads"
  grep -E "^  cpu " "$P/scaling/openmp/cpuscale640-$threads.log" | head -1
done

echo ""
echo "OPENMP 640: done"

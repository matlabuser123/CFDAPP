#!/usr/bin/env bash
# GPU-DISC-001Q -- what does the CPU baseline look like with OpenMP ENABLED?
#
# The production build has CFDAPP_ENABLE_OPENMP=OFF (the project default), so
# every CPU number in this gate is single-threaded -- measured, not assumed; the
# thread sweep on the production build was flat at 1/8/32 threads because the
# pragma is compiled out.
#
# That is the project's own configuration and it is what the GPU-PIPE-001
# baseline used, so old-vs-new is unaffected. But quoting a 10x speed-up against
# a serial CPU on a 32-thread machine without saying so would be exactly the
# "artificially weak CPU baseline" the brief forbids. So: a second build with
# OpenMP ON, the same case, the same settings, reported alongside.
#
# Only the CPU arm is re-measured (`cpuscale`); OMP_NUM_THREADS does not touch
# the GPU arms, so re-running them here would cost time and tell us nothing.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$ROOT/results/gpu-disc-001/performance
CUDA=/usr/local/cuda-12.9
BUILD=$ROOT/build/omp
cd "$ROOT"
mkdir -p "$P/scaling/openmp"

echo "=== configure an OpenMP-enabled build (separate directory; production untouched) ==="
cmake -S . -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCFDAPP_ENABLE_CUDA=ON \
  -DCFDAPP_ENABLE_OPENMP=ON \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.9/bin/nvcc \
  -DBUILD_TESTING=OFF \
  > "$P/scaling/openmp/configure.log" 2>&1
echo "  configure rc=$?"
grep -iE "openmp|found openmp" "$P/scaling/openmp/configure.log" | head -4

echo ""
echo "=== is OpenMP actually compiled in this time? ==="
n=$(grep -c "fopenmp" "$BUILD/build.ninja" || true)
echo "  -fopenmp occurrences in build.ninja: $n"
if [ "$n" -eq 0 ]; then
  echo "  FAIL OpenMP still not compiled in -- this build proves nothing"
  exit 1
fi

echo ""
echo "=== build ==="
ninja -C "$BUILD" > "$P/scaling/openmp/build.log" 2>&1
rc=$?
tail -1 "$P/scaling/openmp/build.log"
echo "  build rc=$rc"
[ $rc -ne 0 ] && { grep -vE '^\[[0-9]+/[0-9]+\]' "$P/scaling/openmp/build.log" | tail -20; exit 1; }

echo ""
echo "=== CPU arm, 320x320, OMP_NUM_THREADS = 1 / 8 / 32 ==="
for threads in 1 8 32; do
  /usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
    -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
    -O2 -DNDEBUG -std=c++20 -fopenmp \
    -o /tmp/gpu_disc_perf_omp_build "$P/tools/performance_benchmark.cpp" \
    -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
    -L"$CUDA/lib64" -lcudart -fopenmp 2> "$P/scaling/openmp/link-$threads.log" || {
      echo "  link failed"; tail -15 "$P/scaling/openmp/link-$threads.log"; exit 1; }
  OMP_NUM_THREADS=$threads /tmp/gpu_disc_perf_omp_build cpuscale \
    > "$P/scaling/openmp/cpuscale-$threads.log" 2>&1
  mv "$P/raw/runs-cpuscale.csv" "$P/scaling/openmp/runs-cpuscale-omp$threads.csv" 2>/dev/null
  printf "  OMP_NUM_THREADS=%-3s " "$threads"
  grep -E "^  cpu " "$P/scaling/openmp/cpuscale-$threads.log" | head -1
done

echo ""
echo "OPENMP CPU BASELINE: done"

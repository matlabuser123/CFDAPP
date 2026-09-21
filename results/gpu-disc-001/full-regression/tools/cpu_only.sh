#!/usr/bin/env bash
# GPU-DISC-001R Phase G -- the CPU backend must work WITHOUT CUDA.
#
# This is the phase the brief calls critical, and the failure it guards against
# is specific: GPU-DISC-001 added ~30 files under cuda/ and include/cfd/gpu/,
# and touched SIMPLE.cpp to dispatch to them. If any of that leaked a hard CUDA
# dependency into CPU-only production code, a CUDA-disabled build would stop
# compiling -- and nothing else in this regression would notice, because every
# other phase builds with CUDA ON.
#
# So: a genuinely separate CUDA-disabled configure, a full build, and the full
# test suite. The stub at src/gpu/GpuSimpleDiscretization.cpp is what must carry
# the CPU-only side, and this is the only thing that exercises it.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/cpu-only
BUILD=$ROOT/build/cpuonly
cd "$ROOT"
mkdir -p "$EVID"

echo "=== configure with CUDA DISABLED, from scratch ==="
rm -rf "$BUILD"
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCFDAPP_ENABLE_CUDA=OFF \
  -DBUILD_TESTING=ON > "$EVID/configure.log" 2>&1
rc=$?
echo "  rc=$rc"
grep -iE "cuda|error" "$EVID/configure.log" | head -6 | sed 's/^/  /'
[ $rc -ne 0 ] && { tail -25 "$EVID/configure.log"; exit 1; }

echo ""
echo "=== confirm CUDA really is off ==="
grep -E "^CFDAPP_ENABLE_CUDA:" "$BUILD/CMakeCache.txt" | sed 's/^/  /'
n=$(grep -c "nvcc" "$BUILD/build.ninja" || true)
echo "  nvcc invocations in build.ninja: $n  (must be 0)"
[ "$n" -ne 0 ] && { echo "  FAIL CUDA is still being compiled"; exit 1; }

echo ""
echo "=== build ==="
ninja -C "$BUILD" > "$EVID/build.log" 2>&1
rc=$?
tail -1 "$EVID/build.log"
echo "  rc=$rc"
if [ $rc -ne 0 ]; then
  echo "  CPU-ONLY BUILD FAILED -- a CUDA dependency leaked into CPU production code"
  grep -vE '^\[[0-9]+/[0-9]+\]' "$EVID/build.log" | tail -30
  exit 1
fi

echo ""
echo "=== does the CPU-only library link against libcudart? (it must NOT) ==="
if ldd "$BUILD/apps/cli/cfdapp" 2>/dev/null | grep -i cudart; then
  echo "  FAIL cfdapp links libcudart in a CUDA-disabled build"
  exit 1
fi
echo "  cfdapp does not link libcudart -- correct"

echo ""
echo "=== full CPU-only test suite ==="
ctest --test-dir "$BUILD" --output-on-failure > "$EVID/ctest.log" 2>&1
rc=$?
grep -E "tests passed|tests failed|Total Test time" "$EVID/ctest.log" | sed 's/^/  /'
echo "  ctest rc=$rc"

echo ""
echo "=== the CPU-only fallback reports itself honestly ==="
grep -rn "without CUDA support" src/gpu/GpuSimpleDiscretization.cpp | head -2 | sed 's/^/  /'

exit $rc

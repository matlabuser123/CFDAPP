#!/usr/bin/env bash
# GPU-DISC-001Q -- the benchmark environment, captured with the runs it explains.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

echo "=== date ==="
date -u +"%Y-%m-%dT%H:%M:%SZ"

echo ""
echo "=== CPU ==="
lscpu | grep -E "^Model name|^CPU\(s\)|^Thread|^Core|^Socket|^CPU max MHz|^CPU min MHz|^L3"
echo "nproc: $(nproc)"

echo ""
echo "=== memory ==="
free -h | head -2

echo ""
echo "=== OS / kernel ==="
. /etc/os-release && echo "$PRETTY_NAME"
uname -r
echo "WSL2: ${WSL_DISTRO_NAME:-unknown}"

echo ""
echo "=== GPU ==="
nvidia-smi --query-gpu=name,memory.total,driver_version,compute_cap,persistence_mode,power.limit,clocks.max.sm,clocks.max.mem \
  --format=csv 2>/dev/null || echo "nvidia-smi unavailable"

echo ""
echo "=== CUDA toolkit ==="
"$CUDA/bin/nvcc" --version | tail -2

echo ""
echo "=== host compiler ==="
/usr/bin/c++ --version | head -1
cmake --version | head -1
ninja --version

echo ""
echo "=== build configuration ==="
grep -E "CMAKE_BUILD_TYPE|CMAKE_CUDA_ARCHITECTURES|CFDAPP_ENABLE_CUDA" build/cuda/CMakeCache.txt \
  | grep -v "ADVANCED" | sed 's/^/  /'
echo "  CUDA flags on discretization kernels: -fmad=false (12 kernels, cuda/CMakeLists.txt)"

echo ""
echo "=== OpenMP ==="
echo "  OMP_NUM_THREADS=${OMP_NUM_THREADS:-<unset>}"
grep -rn "find_package(OpenMP\|OpenMP::OpenMP" CMakeLists.txt src/CMakeLists.txt 2>/dev/null | sed 's/^/  /' \
  || echo "  OpenMP is not linked by the build -- the CPU solver path is SERIAL"

echo ""
echo "=== working-tree identity ==="
echo "  git HEAD: $(git rev-parse HEAD 2>/dev/null || echo 'n/a')"
echo "  tracked modifications (from earlier GPU-PIPE/GPU-DISC gates, uncommitted):"
git status --porcelain 2>/dev/null | grep -v '^?? ' | sed 's/^/    /'

echo ""
echo "=== library identity ==="
ninja -C build/cuda 2>&1 | tail -1
sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a | sed 's/^/  /'
echo "  (NOTE: nvcc is not bit-reproducible here -- see GPU-DISC-001P. The"
echo "   authoritative identity is the SOURCE hash set below.)"
bash results/gpu-disc-001/negative-controls/tools/baseline_sha.sh | sed 's/^/  /'

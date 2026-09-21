#!/usr/bin/env bash
# GPU-PIPE-001 final residency -- environment of record.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/final-residency
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
{
  echo "# GPU-PIPE-001 final residency -- environment"
  echo "date (host)            : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "uname                  : $(uname -srvmo)"
  echo "distro                 : $(. /etc/os-release && echo "$PRETTY_NAME")"
  echo "nproc                  : $(nproc)"
  echo "memory                 : $(free -h | awk 'NR==2{print $2" total, "$7" available"}')"
  echo "c++                    : $(/usr/bin/c++ --version | head -1)"
  echo "cmake                  : $(cmake --version | head -1)"
  echo "ninja                  : $(ninja --version)"
  echo "nvcc                   : $($CUDA/bin/nvcc --version | tail -2 | head -1 | sed 's/^ *//')"
  echo "driver / smi           : $(nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader)"
  echo "compute-sanitizer      : $($CUDA/bin/compute-sanitizer --version 2>&1 | head -2 | tail -1)"
  echo ""
  echo "# build/final cache"
  grep -E "^(CMAKE_BUILD_TYPE|CFDAPP_ENABLE_CUDA|CFDAPP_ENABLE_GUI|CFDAPP_ENABLE_OPENMP|BUILD_TESTING|CMAKE_CUDA_ARCHITECTURES|CMAKE_CUDA_COMPILER|CMAKE_CXX_COMPILER|CMAKE_CXX_FLAGS_RELEASE):" \
    "$BUILD/CMakeCache.txt"
  echo ""
  echo "# git"
  echo "HEAD                   : $(git rev-parse HEAD)"
  echo "origin/main            : $(git rev-parse origin/main 2>/dev/null || echo n/a)"
  echo "tracked modified       : $(git status --porcelain | grep -c '^ M')"
  echo "untracked              : $(git status --porcelain | grep -c '^??')"
} > "$E/environment.txt"
cat "$E/environment.txt"

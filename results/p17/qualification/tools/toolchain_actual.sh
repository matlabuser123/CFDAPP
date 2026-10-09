#!/usr/bin/env bash
# P17.11 -- the toolchain ACTUALLY used by the qualification builds, not the one a bare login shell
# happens to see.
#
# WHY THIS IS SEPARATE: two CUDA toolkits are installed. A bare shell resolves nvcc to the apt
# package (11.5) and compute-sanitizer to 2021.3.1; every qualification harness exports
# /usr/local/cuda-12.9/bin first and therefore uses 12.9 / 2025.2.1.0. The first candidate freeze
# recorded the bare-shell versions, which are NOT what built anything being qualified. Both are
# reported here so the distinction is visible rather than silently resolved.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1

echo "=== as a BARE login shell resolves them (NOT what qualification uses) ==="
echo "  nvcc              : $(command -v nvcc || echo none) -> $(nvcc --version 2>/dev/null | tail -2 | head -1)"
echo "  compute-sanitizer : $(command -v compute-sanitizer || echo none)"

export PATH="/usr/local/cuda-12.9/bin:$PATH"
echo ""
echo "=== as every qualification harness resolves them (PATH=/usr/local/cuda-12.9/bin:...) ==="
echo "  nvcc              : $(command -v nvcc)"
nvcc --version | tail -2 | sed 's/^/                      /'
echo "  compute-sanitizer : $(command -v compute-sanitizer)"
compute-sanitizer --version 2>/dev/null | head -3 | tail -1 | sed 's/^/                      /'

echo ""
echo "=== what the build trees were ACTUALLY compiled with (from their caches) ==="
for d in build/qual-final build/qual-debug; do
  echo "  $(basename "$d"):"
  grep -E '^(CMAKE_CUDA_COMPILER|CMAKE_CUDA_ARCHITECTURES|CMAKE_CXX_COMPILER):' "$d/CMakeCache.txt" 2>/dev/null \
    | sed 's/^/      /'
done
echo "  qual-cpuonly:"
grep -E '^CMAKE_CXX_COMPILER:' build/qual-cpuonly/CMakeCache.txt 2>/dev/null | sed 's/^/      /'
echo "      (CUDA OFF -- no CUDA compiler involved)"

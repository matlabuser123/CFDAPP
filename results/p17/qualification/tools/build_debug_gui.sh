#!/usr/bin/env bash
# P17.11 section 23 -- the Debug+GUI configuration, deferred by every P17 phase to this one.
#
# The tree EXISTS and its options live only in its CMake cache (Debug, GUI ON, CUDA ON, OpenMP OFF).
# It is NOT deleted and NOT reconfigured with bare defaults -- P17.10 learned what that costs.
# The options are re-asserted explicitly so the configuration is recorded in the repo, not only in
# a build directory.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
Q=results/p17/qualification

echo "=== options BEFORE (as cached) ==="
grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_[A-Z_]+):' build/qual-debug/CMakeCache.txt | sort | sed 's/^/  /'

cmake -S . -B build/qual-debug \
  -DCMAKE_BUILD_TYPE=Debug -DCFDAPP_BUILD_GUI=ON -DCFDAPP_ENABLE_CUDA=ON \
  -DCFDAPP_ENABLE_OPENMP=OFF \
  > "$Q/logs/10_debug_gui_configure.log" 2>&1
echo "configure exit: $?"

echo "=== options AFTER ==="
grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_[A-Z_]+):' build/qual-debug/CMakeCache.txt | sort | sed 's/^/  /'

t0=$(date +%s)
cmake --build build/qual-debug -j 16 > "$Q/logs/10_debug_gui_build.log" 2>&1
rc=$?
echo "build exit: $rc   ($(( $(date +%s) - t0 )) s)"
grep -aE '(^|[^a-zA-Z])error:' "$Q/logs/10_debug_gui_build.log" | head -20
exit "$rc"

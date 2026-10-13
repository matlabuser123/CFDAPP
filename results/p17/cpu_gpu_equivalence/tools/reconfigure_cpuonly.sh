#!/usr/bin/env bash
# P17.10 -- restore build/qual-cpuonly to the configuration P17.9 qualified.
#
# WHY THIS EXISTS: the tree was deleted to clear a staleness false positive, and the regression
# harness configures with a bare `cmake -S . -B <dir>`, which on a FRESH tree takes the project
# DEFAULTS -- CMAKE_BUILD_TYPE=Debug and CFDAPP_ENABLE_OPENMP=OFF -- instead of the options the
# tree had been created with. The resulting Debug build ran 5-10x slower and three
# DriverSeegmiller tests hit the 7200 s timeout. That run is INVALID and is preserved as such.
#
# The option set below is taken from build/qual-final's own cache, with CUDA flipped OFF: those two
# trees differ in exactly CFDAPP_ENABLE_CUDA and nothing else.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
P17=results/p17/cpu_gpu_equivalence

cmake -S . -B build/qual-cpuonly \
  -DCMAKE_BUILD_TYPE=Release \
  -DCFDAPP_ENABLE_CUDA=OFF \
  -DCFDAPP_ENABLE_OPENMP=ON \
  -DCFDAPP_BUILD_GUI=OFF \
  -DCFDAPP_BUILD_BENCHMARKS=ON \
  -DCFDAPP_ENABLE_MPI=OFF \
  -DCFDAPP_ENABLE_PACKAGING=OFF \
  -DCFDAPP_ENABLE_SANITIZERS=OFF \
  > "$P17/logs/51_cpuonly_reconfigure.log" 2>&1
echo "configure exit: $?"

echo "--- the options that now apply ---"
grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_[A-Z_]+):' build/qual-cpuonly/CMakeCache.txt | sort | sed 's/^/  /'

echo "--- comparing against qual-final, which must differ ONLY in CUDA ---"
diff <(grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_[A-Z_]+):' build/qual-final/CMakeCache.txt | sort) \
     <(grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_[A-Z_]+):' build/qual-cpuonly/CMakeCache.txt | sort) \
  | sed 's/^/  /'

t0=$(date +%s)
cmake --build build/qual-cpuonly -j 16 > "$P17/logs/51_cpuonly_build.log" 2>&1
rc=$?
echo "build exit: $rc   ($(( $(date +%s) - t0 )) s)"
grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/51_cpuonly_build.log" | head -10
echo "libcfdcore.a: $(sha256sum build/qual-cpuonly/src/libcfdcore.a 2>/dev/null | cut -d' ' -f1)"
echo "P17.9 recorded: 1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a"
exit "$rc"

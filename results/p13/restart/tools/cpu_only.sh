#!/usr/bin/env bash
# P13-001 gate, G4 -- the CPU-only blind spot.
#
# Files under `if(NOT CFDAPP_ENABLE_CUDA)` are compiled by no routine gate, so
# a CUDA-OFF configure is the only thing that proves this item did not break
# the CPU-only build. Configured from scratch, not incrementally.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$ROOT/results/p13/restart/logs
cd "$ROOT"
rm -rf build/p13cpuonly
{ echo "date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"; } > "$L/G4_cpuonly.log"
cmake -S . -B build/p13cpuonly -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCFDAPP_ENABLE_CUDA=OFF -DBUILD_TESTING=ON \
  >> "$L/G4_cpuonly.log" 2>&1
crc=$?
echo "configure rc=$crc" >> "$L/G4_cpuonly.log"
[ $crc -eq 0 ] || { echo "  CONFIGURE FAILED"; tail -20 "$L/G4_cpuonly.log"; exit 1; }
cmake --build build/p13cpuonly -j20 >> "$L/G4_cpuonly.log" 2>&1
brc=$?
echo "build rc=$brc" >> "$L/G4_cpuonly.log"
[ $brc -eq 0 ] || { echo "  BUILD FAILED"; grep -E 'error:' "$L/G4_cpuonly.log" | head -10; exit 1; }
nvcc=$(grep -c 'nvcc' "$L/G4_cpuonly.log")
cudart=$(ldd build/p13cpuonly/apps/cli/cfdapp 2>/dev/null | grep -c cudart)
cudalib=$([ -f build/p13cpuonly/cuda/libcfdcuda.a ] && echo yes || echo no)
{ echo ""; echo "--- proof this build really has no CUDA ---"
  echo "nvcc invocations: $nvcc"; echo "libcudart linked:  $cudart"
  echo "libcfdcuda.a:     $cudalib"; echo ""; echo "--- ctest ---"; } >> "$L/G4_cpuonly.log"
printf '  nvcc invocations %s   libcudart %s   libcfdcuda.a %s\n' "$nvcc" "$cudart" "$cudalib"
[ "$nvcc" -eq 0 ] && [ "$cudart" -eq 0 ] && [ "$cudalib" = no ] || {
  echo "  CUDA leaked into a CUDA-OFF build"; exit 1; }
ctest --test-dir build/p13cpuonly -j16 --output-on-failure >> "$L/G4_cpuonly.log" 2>&1
rc=$?
echo "CTEST_RC=$rc" >> "$L/G4_cpuonly.log"
grep -E "tests passed|tests failed" "$L/G4_cpuonly.log" | sed 's/^/  /'
exit $rc

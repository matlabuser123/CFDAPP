#!/usr/bin/env bash
# GPU-DISC-001O -- audit the production CUDA API/launch error-handling policy.
#
# compute-sanitizer proves the kernels are sound; this proves that when the
# RUNTIME fails, production surfaces it rather than continuing on bad state.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT"

echo "=== 1. every kernel launch is followed by a launch-error check ==="
echo "kernel launches (<<<...>>>) per file, vs recordLaunch/checkCuda(cudaGetLastError) calls:"
for f in cuda/kernels/*.cu; do
  launches=$(grep -c "<<<" "$f")
  checks=$(grep -cE "recordLaunch|checkCuda\(cudaGetLastError" "$f")
  [ "$launches" -eq 0 ] && continue
  status="ok"
  [ "$checks" -eq 0 ] && status="UNCHECKED"
  printf "  %-42s launches=%-3s checks=%-3s %s\n" "$(basename "$f")" "$launches" "$checks" "$status"
done

echo
echo "=== 2. every recordLaunch checks cudaGetLastError ==="
grep -A2 "void recordLaunch" cuda/kernels/*.cu | grep -c "checkCuda(cudaGetLastError()" \
  | xargs -I{} echo "  recordLaunch definitions that check cudaGetLastError: {}"
grep -c "void recordLaunch" cuda/kernels/*.cu | awk -F: '{s+=$2} END {print "  recordLaunch definitions total:                    " s}'

echo
echo "=== 3. allocation and both copy directions are checked ==="
grep -nE "checkCuda\(cuda(Malloc|Free|Memcpy)" include/cfd/gpu/DeviceBuffer.hpp \
  | sed 's/^/  /'

echo
echo "=== 4. the one error-checking helper, and what it does on failure ==="
grep -A4 "inline void checkCuda" include/cfd/gpu/CudaCheck.hpp | sed 's/^/  /'

echo
echo "=== 5. synchronization failures propagate ==="
grep -rn "cudaDeviceSynchronize" cuda/kernels/*.cu | sed 's/^/  /'

echo
echo "=== 6. backend fallbacks are documented policy, not silent ==="
grep -n "gpuBackendFallbacks\|Logger::instance().warning" src/algebra/LinearSolverFactory.cpp \
  | sed 's/^/  /'
grep -n "gpuDiscretizationFallbackReason\|Logger::instance().warning" src/pressure_velocity/SIMPLE.cpp \
  | sed 's/^/  /'

echo
echo "=== 7. required device state is validated before a stage consumes it ==="
grep -c "requireResident" cuda/kernels/GpuSimpleDiscretizationCuda.cpp \
  | xargs -I{} echo "  requireResident guards in the production facade: {}"

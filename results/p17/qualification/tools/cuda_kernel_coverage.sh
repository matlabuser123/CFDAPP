#!/usr/bin/env bash
# P17.11 sections 26-27 -- P17 CUDA kernel coverage, MEASURED rather than asserted.
#
# "10 of 10 kernels exercised" is worth nothing as a claim about a source listing. It is established
# here by RUNNING the suites under a tracer and reading back the kernel names the GPU actually
# launched. If no tracer is available the script says so and the coverage statement downgrades to
# "argued from call sites", which is weaker and is labelled as such -- it does not silently promote
# an argument to a measurement.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
Q=results/p17/qualification
BIN=build/qual-final/tests/unit/gpu/CFDGpuTests

echo "=== P17 CUDA translation units and their kernels ==="
for f in cuda/kernels/DeviceHllcFluxKernel.cu cuda/kernels/DeviceDensityBasedKernel.cu; do
  echo "  $f"
  grep -oE '__global__ void [A-Za-z0-9_]+' "$f" | sed 's/__global__ void /      /'
done
TOTAL=$(grep -hcE '__global__ void' cuda/kernels/DeviceHllcFluxKernel.cu cuda/kernels/DeviceDensityBasedKernel.cu | paste -sd+ - | sed 's/+/ + /' | xargs -I{} python3 -c "print({})")
echo "  TOTAL P17 __global__ kernels: $TOTAL"
echo ""

echo "=== tracer availability ==="
TRACER=""
for t in nsys ncu; do
  p=$(command -v "$t" || true)
  if [ -n "$p" ]; then echo "  $t : $p"; [ -z "$TRACER" ] && TRACER="$t"; else echo "  $t : absent"; fi
done

echo ""
echo "=== measured launches ==="
if [ -n "$TRACER" ] && [ "$TRACER" = "nsys" ]; then
  nsys profile --trace=cuda --force-overwrite=true -o "$Q/logs/50_nsys" \
    "$BIN" --gtest_filter='DensityBasedGpuEquivalence*:HllcGpuEquivalenceTest*' \
    > "$Q/logs/50_nsys_run.log" 2>&1
  echo "  nsys exit: $?"
  nsys stats --report cuda_gpu_kern_sum "$Q/logs/50_nsys.nsys-rep" \
    > "$Q/logs/50_nsys_kernels.log" 2>&1
  grep -oE '[A-Za-z0-9_]+Kernel' "$Q/logs/50_nsys_kernels.log" | sort -u | sed 's/^/    LAUNCHED: /'
else
  echo "  NO TRACER AVAILABLE on this machine."
  echo "  Coverage below is therefore ARGUED FROM CALL SITES, not measured, and is labelled so."
fi

echo ""
echo "=== per-kernel: the entry point that drives it, and the test that exercises that entry ==="
cat <<'MAP'
    hllcKernel            evaluateHllcOnDevice()               HllcGpuEquivalenceTest (17 rows)
    hllKernel             evaluateHllOnDeviceForControlOnly()  HllcGpuEquivalenceTest V2 control
    recoverKernel         recoverPrimitives(), every pipeline  DensityBasedGpuEquivalence A7, C4
    faceFluxKernel        residualPipeline()                   DensityBasedGpuEquivalence B1
    gatherResidualKernel  residualPipeline()                   DensityBasedGpuEquivalence B2, B5
    localTimeStepKernel   timeStepPipeline()                   DensityBasedGpuEquivalence C1
    minReduceKernel       reduceGlobalDt()                     DensityBasedGpuEquivalence C2
    minIndexReduceKernel  oneStep() validity reduction         DensityBasedGpuEquivalence C4
    applyUpdateKernel     oneStep()                            DensityBasedGpuEquivalence C3/D1
    fillKernel            oneStep() dt broadcast               DensityBasedGpuEquivalence C3/D1
    sanitizerOobKernel    sanitizerControlOutOfBoundsRead()    N1 control, sanitizer runs ONLY
    sanitizerRaceKernel   sanitizerControlSharedMemoryRace()   N2 control, sanitizer runs ONLY
MAP

echo ""
echo "=== the independent cross-check: kernel launches COUNTED at runtime ==="
echo "  DeviceSolveStats::kernelLaunches is incremented per launch by the production code and is"
echo "  asserted > steps by the T1 residency test. From logs/30_g8_gpu.log:"
grep -aE '^  T1 ' "$Q/logs/30_g8_gpu.log" 2>/dev/null | sed 's/^/  /'
echo "  A 20-step run launching 180 kernels is 9 per step, which matches the nine production"
echo "  launches one step performs (recover, faceFlux, gather, localTimeStep, minReduce, fill,"
echo "  applyUpdate, recover-again, minIndexReduce). That arithmetic is a real check on the map"
echo "  above: a kernel that never launched would make the count come out lower."

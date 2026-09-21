#!/usr/bin/env bash
# GPU-PIPE-001 Phase 2 -- isolate the 640^2 regression.
#
# Paired measurement showed gpu_solve -19.1% at 160^2, -7.5% at 320^2 but
# +10.2% at 640^2. The suspect is Phase 2A's device finaliser: it is ONE thread
# walking `blocks` partials serially (chosen so the summation order, and hence
# the bits, match the host loop exactly), and blocks goes 100 -> 400 -> 1600
# across those grids. That is a hypothesis, not a conclusion, so test it.
#
# Variant "hostfinal": keep Phase 2B fusion, revert Phase 2A -- download the
# whole partial array and sum on the host, exactly as HEAD did. If the 640^2
# regression disappears, the serial finaliser is the cause.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

SRC=cuda/kernels/DeviceVectorOpsKernel.cu
BACKUP=/tmp/DeviceVectorOpsKernel.cu.orig
PROBE=results/gpu-pipe-001/phase2-reductions/tools/paired_probe.cpp
CUDA_LIB=/usr/local/cuda-12.9/lib64

cleanup() { [ -f "$BACKUP" ] && cp "$BACKUP" "$SRC" && echo "restored $SRC"; }
trap cleanup EXIT
cp "$SRC" "$BACKUP"

build_probe() {
  cmake --build build/cuda --target cfdcore cfdcuda > /tmp/iso_build.log 2>&1 || {
    echo "BUILD FAILED"; grep -iE "error" /tmp/iso_build.log | head -5; exit 1; }
  g++ -std=c++20 -O2 -I include "$PROBE" \
    -Wl,--start-group build/cuda/src/libcfdcore.a build/cuda/cuda/libcfdcuda.a -Wl,--end-group \
    -L"$CUDA_LIB" -lcudart -o /tmp/iso_probe 2>/dev/null
}

measure() { # edge outer repeats -> median gpu_solve_s + d2h
  local edge="$1" outer="$2" reps="$3"
  local times=() line
  for _ in $(seq 1 "$reps"); do
    line=$(/tmp/iso_probe "$edge" "$outer" gpu | grep '^RESULT')
    times+=("$(echo "$line" | grep -oE 'gpu_solve_s=[0-9.]+' | cut -d= -f2)")
    LAST="$line"
  done
  printf '%s\n' "${times[@]}" | sort -n | awk '{a[NR]=$1} END{printf "%.4f", a[int((NR+1)/2)]}'
}

echo "=== A: current Phase 2 (fusion + DEVICE finaliser) ==="
build_probe
for spec in 160:4 640:3; do
  e=${spec%%:*}; o=${spec##*:}
  t=$(measure "$e" "$o" 3)
  echo "  ${e}^2 gpu_solve median ${t}s   d2h=$(echo "$LAST" | grep -oE 'd2h_calls=[0-9]+' | cut -d= -f2) bytes=$(echo "$LAST" | grep -oE 'd2h_bytes=[0-9]+' | cut -d= -f2)"
done

echo
echo "=== B: fusion + HOST finaliser (Phase 2A reverted) ==="
python3 - "$SRC" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """  finalizeSumsKernel<<<1, count>>>(count, static_cast<cfd::Index>(blocks),
                                   partialSumsCache.data(), resultCache.data());
  checkCuda(cudaGetLastError(), "finalizeSumsKernel launch");
  ++gpuExecutionStats().kernelLaunches;

  checkCuda(cudaDeviceSynchronize(), "reduction execution");"""
new = """  checkCuda(cudaDeviceSynchronize(), "reduction execution");"""
assert s.count(old) == 1, "finalize block not found"
s = s.replace(old, new)
old2 = """  resultCache.downloadTo(out, count);"""
new2 = """  // EXPERIMENT (isolate_finalizer.sh): host-side finalisation, as HEAD did.
  {
    std::vector<cfd::Real> hostPartials(static_cast<std::size_t>(blocks) * count);
    partialSumsCache.downloadTo(hostPartials.data(), static_cast<cfd::Index>(blocks) * count);
    for (int q = 0; q < count; ++q) {
      cfd::Real sum = 0.0;
      for (int i = 0; i < blocks; ++i) sum += hostPartials[static_cast<std::size_t>(q) * blocks + i];
      out[q] = sum;
    }
  }"""
assert s.count(old2) == 1, "download line not found"
s = s.replace(old2, new2)
open(p, "w", encoding="utf-8", newline="\n").write(s)
print("  patched to host finalisation")
PY
build_probe
for spec in 160:4 640:3; do
  e=${spec%%:*}; o=${spec##*:}
  t=$(measure "$e" "$o" 3)
  echo "  ${e}^2 gpu_solve median ${t}s   d2h=$(echo "$LAST" | grep -oE 'd2h_calls=[0-9]+' | cut -d= -f2) bytes=$(echo "$LAST" | grep -oE 'd2h_bytes=[0-9]+' | cut -d= -f2)"
done

echo
echo "If B is faster than A at 640^2 but slower at 160^2, the serial device"
echo "finaliser is the 640^2 cost and its price scales with the block count."

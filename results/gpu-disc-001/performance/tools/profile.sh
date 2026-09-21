#!/usr/bin/env bash
# GPU-DISC-001Q -- Nsight Systems profile of the PRODUCTION GPU-DISC path.
#
# The CUDA toolkit installed in WSL is minimal (nvcc + compute-sanitizer, no
# nsys), but Nsight Systems on the Windows side ships a target-linux-x64 agent
# that profiles a WSL binary directly. That is used here rather than settling
# for host-side timers, because the whole point of profiling this path is to get
# DEVICE time per kernel -- which the production stage timers deliberately
# cannot give (they add no synchronization; see audit.md).
#
# THE SPACE TRAP: nsys injects its tracing library via LD_PRELOAD, and the
# Windows install path contains spaces ("NVIDIA Corporation/Nsight Systems").
# ld.so splits LD_PRELOAD on whitespace, so the injection library silently fails
# to load and the capture contains NO CUDA KERNEL DATA -- the run looks
# successful and the report is empty. A space-free symlink fixes it. Found the
# hard way; the first capture produced "does not contain CUDA kernel data".
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/performance/profiling
NSYS_REAL="/mnt/c/Program Files/NVIDIA Corporation/Nsight Systems 2025.1.3/target-linux-x64"
NSYS_LINK=/tmp/nsys/target-linux-x64  # nsys REQUIRES its dir be named target-linux-x64
BIN=/tmp/gpu_disc_perf_profile
cd "$ROOT"
mkdir -p "$EVID"

# A SYMLINK IS NOT ENOUGH: nsys resolves it back to its real install directory
# and still builds LD_PRELOAD from the spaced path. The agent tree has to be
# COPIED somewhere space-free. 476 MB, copied once and reused.
if [ ! -x "$NSYS_LINK/nsys" ]; then
  mkdir -p "$(dirname "$NSYS_LINK")"; rm -rf "$NSYS_LINK"
  cp -r "$NSYS_REAL" "$NSYS_LINK" || exit 1
fi
NSYS="$NSYS_LINK/nsys"

echo "=== nsys ==="
"$NSYS" --version 2>&1 | head -1
echo "invoked from $NSYS_LINK (a space-free COPY, so LD_PRELOAD injection works)"

echo ""
echo "=== profile: 320x320 cavity, gpu-disc arm, 10 outer iterations ==="
# --sample=none: CPU sampling is unreliable under WSL2 and is not what this
# profile is for. --trace=cuda captures kernel and memcpy activity, which is.
"$NSYS" profile \
  --trace=cuda \
  --sample=none \
  --cpuctxsw=none \
  --force-overwrite=true \
  --output="$EVID/gpu-disc-320" \
  "$BIN" profile > "$EVID/profile_run.log" 2>&1
rc=$?
grep -E "LD_PRELOAD|kernels=|Generated" "$EVID/profile_run.log" | head -5
echo "nsys exit=$rc"
[ $rc -ne 0 ] && exit $rc

# Non-vacuity: a capture with no kernel rows proves nothing, and the first
# attempt produced exactly that. Fail loudly rather than reporting an empty
# table as a profiling result.
echo ""
echo "=== kernel time summary (top production GPU time consumers) ==="
"$NSYS" stats --report cuda_gpu_kern_sum --format table --force-export=true "$EVID/gpu-disc-320.nsys-rep" \
  > "$EVID/kernel_summary.txt" 2>&1
# Test for the PRESENCE of kernel rows. An earlier version of this guard
# tested for the absence of one specific error string, and passed on a
# different failure while the report was empty -- a guard that can report
# success on no data is worse than no guard.
kernelRows=$(grep -cE "^ *[0-9]+\.[0-9]|^ *[0-9]+ +[0-9]" "$EVID/kernel_summary.txt" || true)
if [ "${kernelRows:-0}" -lt 1 ]; then
  echo "FAIL the capture contains no CUDA kernel rows -- the profile is VACUOUS"
  cat "$EVID/kernel_summary.txt"
  exit 1
fi
echo "non-vacuity: $kernelRows kernel rows in the summary"
grep -vE "^Processing|^Generating|^$" "$EVID/kernel_summary.txt" | head -28

echo ""
echo "=== memory-operation summary (H2D / D2H on the device timeline) ==="
"$NSYS" stats --report cuda_gpu_mem_time_sum --format table --force-export=true "$EVID/gpu-disc-320.nsys-rep" \
  > "$EVID/memory_summary.txt" 2>&1
grep -vE "^Processing|^Generating|^$" "$EVID/memory_summary.txt" | head -16

echo ""
echo "=== API summary ==="
"$NSYS" stats --report cuda_api_sum --format table --force-export=true "$EVID/gpu-disc-320.nsys-rep" \
  > "$EVID/api_summary.txt" 2>&1
grep -vE "^Processing|^Generating|^$" "$EVID/api_summary.txt" | head -16

echo ""
echo "PROFILING: complete and non-vacuous -- see $EVID"

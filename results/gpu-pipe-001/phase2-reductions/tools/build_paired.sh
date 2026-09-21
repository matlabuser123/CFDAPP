#!/usr/bin/env bash
# GPU-PIPE-001 Phase 2: build the BEFORE and AFTER paired probes.
#
# BEFORE is built from a git worktree at HEAD, OUTSIDE the working tree, so the
# uncommitted Phase-2 sources can never be disturbed by it (CLAUDE.md 2: the
# attribution baseline is an isolated copy). The SAME probe source is used for
# both; it detects the Phase-2C counters at compile time.
set -eu
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

WT=/tmp/cfdapp-before
PROBE=results/gpu-pipe-001/phase2-reductions/tools/paired_probe.cpp
CUDA_LIB=/usr/local/cuda-12.9/lib64

echo "=== BEFORE worktree at HEAD ==="
if [ ! -d "$WT" ]; then
  git worktree add --detach "$WT" HEAD
fi
git -C "$WT" rev-parse --short HEAD

echo "=== configure + build BEFORE (cuda) ==="
cmake -S "$WT" -B "$WT/build/cuda" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCFDAPP_ENABLE_CUDA=ON -DCFDAPP_BUILD_GUI=OFF \
  -DBUILD_TESTING=OFF > /tmp/before_cfg.log 2>&1
cmake --build "$WT/build/cuda" --target cfdcore cfdcuda -- -j20 > /tmp/before_build.log 2>&1
echo "  built: $(ls -la "$WT/build/cuda/cuda/libcfdcuda.a" | awk '{print $5}') bytes"

echo "=== compile BEFORE probe (against HEAD headers + libs) ==="
g++ -std=c++20 -O2 -I "$WT/include" "$PROBE" \
  -Wl,--start-group "$WT/build/cuda/src/libcfdcore.a" "$WT/build/cuda/cuda/libcfdcuda.a" -Wl,--end-group \
  -L"$CUDA_LIB" -lcudart -o /tmp/paired_before
echo "  /tmp/paired_before"

echo "=== compile AFTER probe (against working-tree headers + libs) ==="
cmake --build build/cuda --target cfdcore cfdcuda -- -j20 > /tmp/after_build.log 2>&1
g++ -std=c++20 -O2 -I include "$PROBE" \
  -Wl,--start-group build/cuda/src/libcfdcore.a build/cuda/cuda/libcfdcuda.a -Wl,--end-group \
  -L"$CUDA_LIB" -lcudart -o /tmp/paired_after
echo "  /tmp/paired_after"

echo
echo "=== sanity: both run, and report their own counter availability ==="
/tmp/paired_before 40 2 gpu | sed 's/^/  BEFORE /'
/tmp/paired_after  40 2 gpu | sed 's/^/  AFTER  /'

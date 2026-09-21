#!/usr/bin/env bash
# GPU-PIPE-001 -- two-library attribution of the 40x40 PressureCorrectionFailure.
# CLAUDE.md 6: before blaming (or clearing) a change, build the baseline and
# measure BOTH. HEAD = /tmp/cfdapp-before (a worktree at 548401a, created in
# Phase 2). WORKING = the current tree.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
WT=/tmp/cfdapp-before
SRC=results/gpu-pipe-001/equivalence/tools/attribution_40x40.cpp
CUDA_LIB=/usr/local/cuda-12.9/lib64

echo "HEAD worktree at: $(git -C "$WT" rev-parse --short HEAD)"
cmake --build "$WT/build/cuda" --target cfdcore cfdcuda -- -j20 > /tmp/attr_head_build.log 2>&1 \
  || { echo "HEAD build failed"; tail -3 /tmp/attr_head_build.log; exit 1; }
g++ -std=c++20 -O2 -I "$WT/include" "$SRC" \
  -Wl,--start-group "$WT/build/cuda/src/libcfdcore.a" "$WT/build/cuda/cuda/libcfdcuda.a" -Wl,--end-group \
  -L"$CUDA_LIB" -lcudart -o /tmp/attr_head || exit 1

cmake --build build/cuda --target cfdcore cfdcuda -- -j20 > /tmp/attr_work_build.log 2>&1 \
  || { echo "working build failed"; tail -3 /tmp/attr_work_build.log; exit 1; }
g++ -std=c++20 -O2 -I include "$SRC" \
  -Wl,--start-group build/cuda/src/libcfdcore.a build/cuda/cuda/libcfdcuda.a -Wl,--end-group \
  -L"$CUDA_LIB" -lcudart -o /tmp/attr_work || exit 1

EDGE=${1:-40}
OUTER=${2:-3000}
echo
echo "=== HEAD (548401a, before GPU-PIPE-001) -- ${EDGE}x${EDGE}, ${OUTER} outer ==="
/tmp/attr_head "$EDGE" "$OUTER"
echo
echo "=== WORKING TREE (Phase 2 + 3 + rho-carry) -- ${EDGE}x${EDGE}, ${OUTER} outer ==="
/tmp/attr_work "$EDGE" "$OUTER"
echo
echo "status: 0=Converged 1=MaxIterations 2=MomentumFailure 3=PressureCorrectionFailure"
echo "If HEAD's GPU arm also reports 3, the behaviour predates GPU-PIPE-001."

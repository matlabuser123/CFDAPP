#!/usr/bin/env bash
# GPU-PIPE-001 Phase 2, Gate 1: build and run the reduction identity probe
# against the real libraries.
set -eu
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
OUT=/tmp/reduction_identity
g++ -std=c++20 -O2 -I include \
  results/gpu-pipe-001/phase2-reductions/tools/reduction_identity.cpp \
  build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a \
  -L/usr/local/cuda-12.9/lib64 -lcudart -o "$OUT"
echo "built $OUT"
"$OUT"

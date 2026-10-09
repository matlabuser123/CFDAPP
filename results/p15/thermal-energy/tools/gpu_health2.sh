#!/usr/bin/env bash
# P15 -- GPU restored. Record the environment before any GPU gate runs.
export PATH=/usr/local/cuda-12.9/bin:$PATH
echo "=== nvidia-smi ==="
nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>&1 | head -3
echo "=== driver CUDA capability ==="
nvidia-smi 2>&1 | grep -o "CUDA Version: [0-9.]*" | head -1
echo "=== /dev/dxg ==="
ls -l /dev/dxg 2>&1 | head -2
echo "=== nvcc (the QUALIFIED toolchain -- not changed) ==="
nvcc --version 2>&1 | tail -2
echo "=== existing CUDA test binary still runs? ==="
B=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/build/final/tests/unit/gpu/CFDGpuTests
if [ -x "$B" ]; then "$B" --gtest_filter='*Device*' 2>&1 | tail -5; else echo "no CFDGpuTests binary at $B"; fi

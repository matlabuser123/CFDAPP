#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
./build/cuda/tests/unit/gpu/CFDGpuTests --gtest_filter='DeviceScalarTransportSemantics.*' 2>&1 | tail -80

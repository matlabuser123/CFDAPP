#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
cmake -S . -B build/final 2>&1 | tail -8
grep -c "DeviceScalarTransportKernel" build/final/build.ninja

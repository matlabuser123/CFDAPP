#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
cmake --build build/final -j 12 2>&1 | grep -E "error|FAILED" | head -25
cmake --build build/final -j 12 2>&1 | tail -2

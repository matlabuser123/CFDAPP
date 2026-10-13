#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format --style=file -i src/app/ProjectRunner.cpp
cmake --build build/final -j 12 2>&1 | tail -2
bash results/p13/diagnostics/tools/verify_V_L.sh

#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i include/cfd/turbulence/WallTreatment.hpp \
  src/turbulence/WallTreatment.cpp include/cfd/algebra/SparseMatrix.hpp src/algebra/SparseMatrix.cpp
cmake -S . -B build/final > /dev/null 2>&1
cmake --build build/final --target cfdcore -j 16 2>&1 | grep -E "error:|FAILED" | head -25
echo "--- core build finished ---"

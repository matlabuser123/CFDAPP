#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i include/cfd/turbulence/SSTModel.hpp src/turbulence/SSTModel.cpp \
  src/io/CaseBuilder.cpp src/io/case/PhysicsConfigParser.cpp
cmake -S . -B build/final > /dev/null 2>&1
cmake --build build/final --target cfdcore -j 16 2>&1 | grep -E "error:|FAILED" | head -25
echo "--- core done ---"

#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i include/cfd/transport/ScalarTransport.hpp \
  src/transport/ScalarTransport.cpp include/cfd/discretization/TimeDerivative.hpp \
  src/discretization/TimeDerivative.cpp
cmake --build build/final -j 12 2>&1 | grep -E "error|FAILED" | head -20
cmake --build build/final -j 12 2>&1 | tail -2

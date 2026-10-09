#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i src/io/case/PhysicsConfigParser.cpp include/cfd/io/case/PhysicsConfig.hpp 2>/dev/null
cmake -S . -B build/final > /dev/null 2>&1
if ! cmake --build build/final --target cfdcore -j 20 > /tmp/p15_core.log 2>&1; then
  echo "BUILD FAILED"; grep -E "error:" /tmp/p15_core.log | head -15; exit 1
fi
echo "BUILD OK"

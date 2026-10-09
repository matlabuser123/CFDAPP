#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i src/thermal/EnergyEquation.cpp include/cfd/thermal/EnergyEquation.hpp
cmake --build build/final --target cfdcore -j 12 2>&1 | grep -E "error:" | head -12
cmake --build build/final --target cfdcore -j 12 2>&1 | tail -1

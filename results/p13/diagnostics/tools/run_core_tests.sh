#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format --style=file -i tests/unit/core/test_logger.cpp
cmake --build build/final -j 12 2>&1 | tail -2
cd build/final
ctest -R "Logger|LogLevel" --output-on-failure 2>&1 | tail -30

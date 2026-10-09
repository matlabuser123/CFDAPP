#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
cmake --build build/final -j 12 2>&1 | tail -3
cd build/final
ctest -R "CFDAppCliLogLevel|CFDAppCliLogLines|CFDAppCliCheckpoint_log_level|CFDAppCliValidCase" --output-on-failure 2>&1 | tail -25

#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
rm -f /tmp/p14_perf
g++ -std=c++20 -O2 -DNDEBUG -I include -I build/final/generated/include \
    -I /usr/local/cuda-12.9/include results/p14/transport-foundation/tools/perf_residency.cpp \
    -o /tmp/p14_perf -Wl,--start-group build/final/src/libcfdcore.a \
    build/final/cuda/libcfdcuda.a -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart
rc=$?; [ $rc -eq 0 ] && [ -x /tmp/p14_perf ] || { echo "COMPILE FAILED ($rc)"; exit 1; }
/tmp/p14_perf

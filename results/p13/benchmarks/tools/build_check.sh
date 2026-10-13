#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i benchmarks/BenchmarkOutput.hpp benchmarks/cpu/*.cpp benchmarks/gpu/*.cpp
cmake --build build/final -j 12 2>&1 | grep -E "error|FAILED" | head -15
cmake --build build/final -j 12 2>&1 | tail -2
echo "=== does --output-dir actually redirect? ==="
W=/tmp/p13_outdir; rm -rf $W; mkdir -p $W
cd /tmp
/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/build/final/benchmarks/cpu/cfd_benchmark_cpu --output-dir $W > /dev/null 2>&1
echo "  exit=$? files in $W: $(ls $W | wc -l)"
ls $W | head -5 | sed 's/^/    /'
echo "=== a missing argument is an error, not a silent default ==="
/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/build/final/benchmarks/cpu/cfd_benchmark_cpu --output-dir 2>&1 | head -2
echo "  exit=$?"

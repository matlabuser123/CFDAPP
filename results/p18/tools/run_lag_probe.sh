#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P=results/p18
B=build/qual-final
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P/tools/boundary_lag_probe.cpp" -o /tmp/p18_lag \
    -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" -Wl,--end-group \
    -L/usr/local/cuda-12.9/lib64 -lcudart 2> "$P/logs/10_lag_build.log"
rc=$?
echo "build exit: $rc"
if [ "$rc" -ne 0 ]; then grep -E "error:" "$P/logs/10_lag_build.log" | head -15; exit "$rc"; fi
OMP_NUM_THREADS=1 /tmp/p18_lag

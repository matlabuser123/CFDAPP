#!/usr/bin/env bash
# P17.11 sections 30/31/45 -- build and run the performance characterization.
# No gate depends on any number this produces.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P=results/p17/performance
B=build/qual-final
JSON=$B/_deps/nlohmann_json-src/include

g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P/tools/p17_perf.cpp" -o /tmp/p17_perf \
    -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" -Wl,--end-group \
    -L/usr/local/cuda-12.9/lib64 -lcudart 2> "$P/logs/90_perf_build.log"
rc=$?
echo "build exit: $rc"
if [ "$rc" -ne 0 ]; then tail -20 "$P/logs/90_perf_build.log"; exit "$rc"; fi
echo "libcfdcore.a sha256: $(sha256sum $B/src/libcfdcore.a | awk '{print $1}')"
echo ""
OMP_NUM_THREADS=1 /tmp/p17_perf

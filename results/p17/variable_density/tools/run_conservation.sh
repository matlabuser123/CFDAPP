#!/usr/bin/env bash
# P17.1 conservation measurement -- reports numbers next to the derived bound.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/variable_density
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/07_conservation.log
{
echo "P17.1 conservation measurement"
echo "date: $(date -u +%FT%TZ)"
echo "source under test: $(sha256sum src/compressible/CompressibleMassFlux.cpp | awk '{print $1}')"
echo ""
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/conservation.cpp" \
    -o /tmp/p17_1_cons -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/07_conservation_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "COMPILE FAILED (rc=$rc)"
  tail -25 "$P17/logs/07_conservation_build.log" | sed 's/^/  /'
else
  /tmp/p17_1_cons
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"

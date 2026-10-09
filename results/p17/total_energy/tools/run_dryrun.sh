#!/usr/bin/env bash
# P17.5 dry-run against the UNCHANGED tree. Read-only on production source.
# qual-final is Release + CUDA + OpenMP, so -fopenmp and the CUDA runtime are both required.
# OMP_NUM_THREADS=1 is the frozen execution policy.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/total_energy
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/01_dryrun.log
{
echo "P17.5 dry-run -- the UNCHANGED tree"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse --short=12 HEAD)"
echo ""
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/dryrun.cpp" \
    -o /tmp/p17_5_dryrun -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/01_dryrun_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "  COMPILE FAILED (rc=$rc)"
  tail -40 "$P17/logs/01_dryrun_build.log" | sed 's/^/    /'
else
  OMP_NUM_THREADS=1 /tmp/p17_5_dryrun | sed 's/^/  /'
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"

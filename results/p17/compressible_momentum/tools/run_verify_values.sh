#!/usr/bin/env bash
# Build and run tools/verify_values.cpp against the IMPLEMENTED tree.
# Same link recipe as run_dryrun.sh: qual-final is Release + CUDA + OpenMP, so -fopenmp and
# the CUDA runtime are both required. OMP_NUM_THREADS=1 is the frozen execution policy and it
# avoids the oversubscription thrash that stalled a P17.1 probe for 13 minutes.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_momentum
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/10_verify_values.log
{
echo "P17.4 measured values -- post-implementation"
echo "date: $(date -u +%FT%TZ)"
echo "src/compressible/DilatationalStress.cpp sha256: $(sha256sum src/compressible/DilatationalStress.cpp | awk '{print $1}')"
echo ""
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/verify_values.cpp" \
    -o /tmp/p17_4_verify -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/10_verify_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "  COMPILE FAILED (rc=$rc)"
  tail -30 "$P17/logs/10_verify_build.log" | sed 's/^/    /'
else
  OMP_NUM_THREADS=1 /tmp/p17_4_verify | sed 's/^/  /'
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"

#!/usr/bin/env bash
# P17.2 verification probe -- identity residuals AFTER the change, with numbers.
# OMP_NUM_THREADS=1 on the probe: frozen execution policy, and it avoids the
# oversubscription thrash that stalled a P17.1 probe.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/ideal_gas_eos
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/08_verify.log
{
echo "P17.2 verification -- identity residuals AFTER the change"
echo "date: $(date -u +%FT%TZ)"
echo "source under test:"
sha256sum src/compressible/IdealGasEOS.cpp src/compressible/ThermodynamicProperties.cpp \
          include/cfd/compressible/EquationOfState.hpp | sed 's/^/  /'
echo ""
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/verify.cpp" \
    -o /tmp/p17_2_verify -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/08_verify_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "COMPILE FAILED (rc=$rc)"
  tail -25 "$P17/logs/08_verify_build.log" | sed 's/^/  /'
else
  OMP_NUM_THREADS=1 /tmp/p17_2_verify
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"

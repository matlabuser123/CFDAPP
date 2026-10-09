#!/usr/bin/env bash
# P17.2 dry-run against the UNCHANGED tree. Read-only on production source.
# build/qual-final = Release + CUDA + OpenMP (the P13 qualification tree, NOT build/final
# which is CUDA=OFF). -fopenmp is required because qual-final's libcfdcore.a has it.
# OMP_NUM_THREADS=1 on the probe itself: the frozen execution policy, and it avoids the
# oversubscription thrash that stalled a P17.1 probe.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/ideal_gas_eos
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/00_dryrun.log
{
echo "P17.2 dry-run -- the UNCHANGED tree"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse --short=12 HEAD)"
echo "P17_CODE_FINGERPRINT at the P17.1 close: eba3d7023f8c2516884e260ca946f4440b82e2557e5dbbaa3cce32f7c6432935"
echo ""

echo "### the EquationOfState interface as it stands ###"
grep -nE 'virtual .*\) const = 0;' include/cfd/compressible/EquationOfState.hpp | sed 's/^/    /'
echo "  temperature() present? $(grep -c 'temperature(Real pressure\|temperature(Real density' include/cfd/compressible/EquationOfState.hpp)  (0 = absent, the baseline)"
echo "  implementations of EquationOfState:"
grep -rln ': public EquationOfState' include src | sed 's/^/    /'
echo ""

echo "### GPU EOS arithmetic (0 everywhere = no duplication) ###"
for s in gasConstant speedOfSound idealGas; do
  printf '    %-16s %s\n' "$s" "$(grep -rniI -- "$s" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo ""

echo "### existing EOS + thermodynamics test counts ###"
printf '    %-56s %s\n' tests/unit/compressible/test_ideal_gas_eos.cpp \
  "$(grep -cE '^TEST' tests/unit/compressible/test_ideal_gas_eos.cpp)"
printf '    %-56s %s\n' tests/unit/compressible/test_thermodynamic_properties.cpp \
  "$(grep -cE '^TEST' tests/unit/compressible/test_thermodynamic_properties.cpp)"
echo ""

echo "### measured behaviour ###"
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/dryrun.cpp" \
    -o /tmp/p17_2_dryrun -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/00_dryrun_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "  COMPILE FAILED (rc=$rc)"
  tail -25 "$P17/logs/00_dryrun_build.log" | sed 's/^/    /'
else
  OMP_NUM_THREADS=1 /tmp/p17_2_dryrun | sed 's/^/  /'
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"

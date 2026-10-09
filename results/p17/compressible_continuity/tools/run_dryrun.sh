#!/usr/bin/env bash
# P17.3 dry-run against the UNCHANGED tree. Read-only on production source.
# build/qual-final = Release + CUDA + OpenMP (the P13 qualification tree, NOT build/final
# which is CUDA=OFF). -fopenmp is required because qual-final's libcfdcore.a has it.
# OMP_NUM_THREADS=1 on the probe itself: the frozen execution policy, and it avoids the
# oversubscription thrash that stalled a P17.1 probe.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_continuity
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/00_dryrun.log
{
echo "P17.3 dry-run -- the UNCHANGED tree"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse --short=12 HEAD)"
echo "P17_CODE_FINGERPRINT at the P17.2 close: 4d08c2367c1d24fca640f2c4d3ec47084cfdc6ba52788972edaf2a19d3704491"
echo ""

echo "### the continuity evaluators as they stand ###"
echo "  globalNetFlux in CompressibleContinuityResult: $(grep -c globalNetFlux include/cfd/compressible/CompressibleContinuity.hpp)  (0 = absent, the baseline)"
echo "  value checks in evaluateCompressibleContinuity: $(grep -cE 'isfinite\((massFlux|density)' src/compressible/CompressibleContinuity.cpp)  (0 = absent)"
echo "  value checks in physics::evaluateContinuity   : $(grep -cE 'isfinite\((massFlux|density)' src/physics/ContinuityEquation.cpp)  (0 = absent)"
echo "  production caller of evaluateCompressibleContinuity: $(grep -rl evaluateCompressibleContinuity src/ apps/ | grep -v CompressibleContinuity.cpp | wc -l)  (0 = diagnostic only)"
echo ""

echo "### GPU continuity arithmetic (0 everywhere = no duplication) ###"
for s in compressible continuity massFlux; do
  printf '    %-16s %s\n' "$s" "$(grep -rniI -- "$s" cuda/kernels/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo ""

echo "### existing continuity test counts ###"
printf '    %-56s %s\n' tests/unit/compressible/test_compressible_continuity.cpp \
  "$(grep -cE '^TEST' tests/unit/compressible/test_compressible_continuity.cpp)"
printf '    %-56s %s\n' tests/unit/compressible/test_compressible_mass_flux.cpp \
  "$(grep -cE '^TEST' tests/unit/compressible/test_compressible_mass_flux.cpp)"
echo ""

echo "### measured behaviour ###"
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/dryrun.cpp" \
    -o /tmp/p17_3_dryrun -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/00_dryrun_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "  COMPILE FAILED (rc=$rc)"
  tail -25 "$P17/logs/00_dryrun_build.log" | sed 's/^/    /'
else
  OMP_NUM_THREADS=1 /tmp/p17_3_dryrun | sed 's/^/  /'
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"

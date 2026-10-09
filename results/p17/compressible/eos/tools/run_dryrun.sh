#!/usr/bin/env bash
# P17.2 dry-run. Uses build/qual-final (Release + CUDA + OpenMP), the P13
# qualification tree -- NOT build/final, which is CUDA=OFF. Four P13 gates were
# tripped by that distinction; see the G9 report.
# qual-final's libcfdcore.a is built with OpenMP, so -fopenmp is required on the
# link line (the omission that broke G9's first fingerprint attempt).
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible/eos
B=build/qual-final
cd "$ROOT"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

out=$P17/logs/00_dryrun.log
mkdir -p "$P17/logs"
{
echo "P17.2 dry-run -- the UNCHANGED tree"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse --short=12 HEAD)"
echo "SOURCE_FINGERPRINT_V2: $(bash results/p13/final-qualification/2026-09-24/g4-cli-gui/tools/fingerprint_v2.sh P17_2_dryrun | grep -oE '[0-9a-f]{64}' | head -1)"
echo ""

echo "### the interface as it stands ###"
echo "  EquationOfState virtual methods:"
grep -nE 'virtual .*\) const = 0;' include/cfd/compressible/EquationOfState.hpp | sed 's/^/    /'
echo "  implementations of EquationOfState:"
grep -rln ': public EquationOfState' include src | sed 's/^/    /'
echo "  pressure(rho,T) present? $(grep -c 'pressure(Real\|pressure(double' include/cfd/compressible/EquationOfState.hpp) occurrences  (0 = absent, the baseline)"
echo ""

echo "### existing compressible test inventory ###"
tot=0
for f in $(find tests -name '*compressible*' -o -name '*low_mach*' | grep '\.cpp$' | sort); do
  n=$(grep -c '^TEST' "$f")
  tot=$((tot + n))
  printf '    %-62s %s\n' "$f" "$n"
done
echo "  total: $tot tests"
echo ""

echo "### reference values, %.17g so 'unchanged' is a string comparison later ###"
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/dryrun.cpp" \
    -o /tmp/p17_2_dryrun -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/00_dryrun_build.log"
if [ $? -ne 0 ]; then
  echo "  COMPILE FAILED"; tail -12 "$P17/logs/00_dryrun_build.log" | sed 's/^/    /'
else
  /tmp/p17_2_dryrun | sed 's/^/  /'
fi
echo ""
echo "### the pressure convention, as already documented ###"
grep -n 'gauge pressure' include/cfd/compressible/CompressibleSIMPLE.hpp | sed 's/^/    /'
grep -n 'referencePressure converts' include/cfd/compressible/CompressibleSIMPLE.hpp include/cfd/io/case/PhysicsConfig.hpp | sed 's/^/    /'
grep -n 'pressureAbsolute' include/cfd/app/ProjectRunner.hpp | sed 's/^/    /'
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"

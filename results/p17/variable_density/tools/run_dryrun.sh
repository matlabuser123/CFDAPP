#!/usr/bin/env bash
# P17.1 dry-run against the UNCHANGED tree. Read-only on production source.
# Build tree: build/qual-final (Release + CUDA + OpenMP) -- the P13 qualification tree,
# NOT build/final which is CUDA=OFF. qual-final's libcfdcore.a is built with OpenMP so
# -fopenmp is required on the link line. Both facts are P13 lessons, reused verbatim
# from results/p17/compressible/eos/tools/run_dryrun.sh.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/variable_density
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/00_dryrun.log
{
echo "P17.1 dry-run -- the UNCHANGED tree"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse --short=12 HEAD)"
echo "P17_CODE_FINGERPRINT (P17.0 baseline): 1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2"
echo ""

echo "### the validators as they stand -- density VALUES are unchecked ###"
echo "  validateFaceDensityInputs checks:"
grep -nE 'size\(\) !=|isfinite|> 0\.0' src/compressible/CompressibleMassFlux.cpp | sed 's/^/    /'
echo ""
echo "  density-VALUE checks per compressible module (0 everywhere is the baseline):"
for f in CompressibleMassFlux CompressibleRelaxedMomentum CompressiblePressureCorrection \
         CompressibleMomentum CompressibleContinuity; do
  printf '    %-38s %s\n' "$f.cpp" \
    "$(grep -cE 'density.*isfinite|isfinite.*density|requirePositive' "src/compressible/$f.cpp")"
done
echo ""

echo "### existing negative tests for a bad density VALUE (0 = the gap) ###"
printf '    non-positive/non-finite DENSITY tests: %s\n' \
  "$(grep -crE 'NonPositiveDensity|NonFiniteDensity|NegativeDensity' tests/unit/compressible/test_compressible_mass_flux.cpp)"
printf '    size-mismatch tests (present):         %s\n' \
  "$(grep -cE 'Mismatched.*SizeThrows' tests/unit/compressible/test_compressible_mass_flux.cpp)"
echo ""

echo "### measured behaviour ###"
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include "$P17/tools/dryrun.cpp" \
    -o /tmp/p17_1_dryrun -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/00_dryrun_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "  COMPILE FAILED (rc=$rc)"
  tail -25 "$P17/logs/00_dryrun_build.log" | sed 's/^/    /'
else
  /tmp/p17_1_dryrun | sed 's/^/  /'
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"

#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G10 steps 2-5: the three full regressions.
#
# FROZEN, copied verbatim, not re-derived:
#   exclusions        results/p16/turbulence/tools/regress_both.sh, "p16" mode
#                     (4 deferred automatic-wall suites + the preserved failing
#                      DriverSeegmiller.CorrectedMeshSequence) -- exactly the set
#                      PLAN_2026-09-24.md's G2 entry documents
#   invocation        OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200   <- the prefix is
#                     part of the FROZEN policy, not an optimisation. AMENDMENT_G3
#                     measured it: unset (=nproc=32) 304.95 s and FAILED a 300 s cap
#                     where =1 took 1.72 s. G10 attempt 1 omitted it and died after
#                     5h38m at 64/2537 with 7 contention timeouts, load 255.94.
#                     Preserved under logs/PRESERVED_invalid_no_omp_limit/.
#                     G3's amendment: its first attempt used -j16 with the default
#                     1500 s timeout and produced 250 timeouts / 0 assertion
#                     failures. That invalid invocation is preserved.
#
# BUILD TREES: the three canonical G1 qualification trees, not regress_both.sh's
# build/cuda + build/final. build/final is CFDAPP_ENABLE_CUDA=OFF and therefore
# cannot be the Release+CUDA configuration. Recorded in source_fingerprint.md.
#
# Each configuration is rebuilt first, then tested. Results are parsed from the
# ctest log rather than from a pipeline exit code -- P13-005's final_ctest.sh
# records why `| tail -N; echo RC=$?` is not evidence.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G10=$ROOT/results/p13/final-qualification/2026-09-24/g10-closeout
L=$G10/logs
cd "$ROOT"
mkdir -p "$L" "$G10/data"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'

log=$L/G10_regress.log
{
echo "P13 G10 -- full regression, three canonical configurations"
echo "date: $(date -u +%FT%TZ)"
echo ""
echo "exclusions (frozen, regress_both.sh p16 mode):"
echo "  $EXCLUDE"
echo "invocation (frozen): OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200"
echo ""

run_cfg() {   # run_cfg <label> <build dir>
  local label="$1" dir="$2"
  echo "############################################################"
  echo "### $label  ($dir)"
  echo "############################################################"
  grep -hE 'CMAKE_BUILD_TYPE:STRING|CFDAPP_ENABLE_CUDA:BOOL|CFDAPP_BUILD_GUI:BOOL|CFDAPP_ENABLE_OPENMP:BOOL' \
    "$dir/CMakeCache.txt" | sed 's/^/  /'
  echo ""
  echo "--- build ---"
  local t0=$(date +%s)
  cmake --build "$dir" -j 16 > "$L/G10_${label}_build.log" 2>&1
  local brc=$?
  echo "  build exit: $brc   ($(( $(date +%s) - t0 )) s)"
  grep -aE '(^|[^a-zA-Z])error:|FAILED' "$L/G10_${label}_build.log" | head -10 | sed 's/^/    /'
  if [ $brc -ne 0 ]; then echo "  *** BUILD FAILED -- not testing this configuration ***"; echo ""; return 1; fi
  echo ""
  echo "--- OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 ---"
  t0=$(date +%s)
  OMP_NUM_THREADS=1 ctest --test-dir "$dir" -j 8 --timeout 7200 -E "$EXCLUDE" --output-on-failure \
    > "$L/G10_${label}_ctest.log" 2>&1
  local crc=$?
  local secs=$(( $(date +%s) - t0 ))
  local total pct failed notrun disabled
  total=$(grep -aoE 'out of [0-9]+' "$L/G10_${label}_ctest.log" | tail -1 | grep -oE '[0-9]+')
  pct=$(grep -aoE '[0-9]+% tests passed' "$L/G10_${label}_ctest.log" | tail -1)
  failed=$(grep -aoE '[0-9]+ tests failed' "$L/G10_${label}_ctest.log" | tail -1 | grep -oE '^[0-9]+')
  notrun=$(grep -acE '\(Not Run\)' "$L/G10_${label}_ctest.log" || true)
  disabled=$(grep -acE '\(Disabled\)' "$L/G10_${label}_ctest.log" || true)
  echo "  ctest exit status : $crc"
  echo "  total tests       : ${total:-unknown}"
  echo "  passed            : ${pct:-unknown}"
  echo "  failed            : ${failed:-0}"
  echo "  Not Run           : $notrun"
  echo "  Disabled          : $disabled"
  echo "  runtime           : ${secs} s ($(awk -v s=$secs 'BEGIN{printf "%.1f", s/60}') min)"
  if [ "${failed:-0}" != "0" ] || [ "$crc" -ne 0 ]; then
    echo "  --- every non-passing test ---"
    grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/G10_${label}_ctest.log" | sed 's/^/    /'
  fi
  echo ""
}

run_cfg release_cuda build/qual-final
run_cfg debug_gui    build/qual-debug
run_cfg cpu_only     build/qual-cpuonly

echo "############################################################"
echo "### CPU-only: no accidental CUDA dependency (step 4)"
echo "############################################################"
echo "  nvcc invocations in the build log : $(grep -ac 'nvcc' "$L/G10_cpu_only_build.log" || true)"
echo "  libcudart references              : $(grep -ac 'libcudart' "$L/G10_cpu_only_build.log" || true)"
echo "  cuda/libcfdcuda.a present         : $([ -f build/qual-cpuonly/cuda/libcfdcuda.a ] && echo YES || echo no)"
echo "  binaries linking libcudart        :"
for b in build/qual-cpuonly/apps/cli/cfdapp; do
  [ -x "$b" ] && printf '    %-42s %s\n' "$b" "$(ldd "$b" 2>/dev/null | grep -c cudart)"
done
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

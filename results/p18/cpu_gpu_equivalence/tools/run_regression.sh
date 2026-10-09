#!/usr/bin/env bash
# P18.8 §57 -- the three-configuration regression harness.
#
# EXECUTION POLICY: inherited VERBATIM from P18.7, which inherited it from P17.10, which inherited
# it from P13's G10 closeout:
#
#     OMP_NUM_THREADS=1  ctest -j 8  --timeout 7200  -E <the same 5 frozen exclusions>
#
# NOT A CHOICE, AND NOT NEGOTIABLE. Attempt 1 of this regression deviated on all three points --
# no OMP_NUM_THREADS, -j 16, no exclusions -- and produced 15 timeouts against a baseline of 0
# failures, because 16 heavyweight tests ran concurrently with each free to spawn OpenMP threads
# across all 32 logical CPUs. The same tests inflated by 3.8x to 62x. See
# logs/regression_INVALID_AS_AUTHORITATIVE.md. A regression run under different execution conditions
# than the baseline is not comparable to the baseline, whatever it reports.
#
# IT FAILS CLOSED. CLAUDE.md §8: a regression result is authoritative only if the test binaries were
# demonstrably rebuilt against the exact source being tested, so the four qualification options are
# ASSERTED against each cache and any mismatch means that configuration is not tested at all.
#
# Usage:  run_regression.sh <output-directory>
set -u

OUTDIR="${1:?usage: run_regression.sh <output-directory>}"
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1
mkdir -p "$OUTDIR"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

BUILD_JOBS=16   # P18.7 used -j 16 for the build; the hardware policy allows 16-24
CTEST_JOBS=8    # INHERITED. Do not raise.

# The 5 frozen exclusion patterns, byte-identical to P18.7's.
EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'

# label:dir:build:gui:cuda:openmp  -- the expectations P18.7 asserted, with the CORRECT cache
# variable names. Attempt 1 queried CFDAPP_ENABLE_GUI, which does not exist, and so silently
# mislabelled the Debug configuration; the real name is CFDAPP_BUILD_GUI.
CONFIGS="release_cuda:build/qual-final:Release:OFF:ON:ON
cpu_only:build/qual-cpuonly:Release:OFF:OFF:ON
debug_gui:build/qual-debug:Debug:ON:ON:OFF"

newest_production() {
  find include src cuda apps -type f \
    \( -name '*.hpp' -o -name '*.h' -o -name '*.cpp' -o -name '*.cu' -o -name '*.cuh' \
       -o -name 'CMakeLists.txt' \) -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1
}

SUMMARY="$OUTDIR/00_summary.txt"
{
  echo "P18.8 §57 -- THREE-CONFIGURATION REGRESSION (attempt 2)"
  echo "started (WSL): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "host: nproc=$(nproc), $(free -h | awk '/^Mem:/{print $2}') RAM visible to WSL"
  echo "git HEAD: $(git rev-parse HEAD)"
  echo ""
  echo "EXECUTION POLICY (inherited verbatim, see the header):"
  echo "  OMP_NUM_THREADS=1  ctest -j $CTEST_JOBS  --timeout 7200  -E <5 frozen exclusions>"
  echo "  exclusions: $EXCLUDE"
  echo "  build jobs: $BUILD_JOBS"
  echo ""
  echo "newest production source at start: $(newest_production)"
  echo ""
  echo "BASELINE, from P18.7's own recorded log (results/p18/conservation_validation/logs/):"
  echo "  release_cuda  registered 3041  ran 2979  passed 2977  failed 0  skipped 2  exit 0"
  echo "  cpu_only      registered 2818  ran 2758  passed 2758  failed 0  skipped 0  exit 0"
  echo "  debug_gui     registered 3103  ran 3041  passed 3036  failed 3  skipped 2  exit 8"
  echo ""
} >"$SUMMARY"

overall=0

echo "$CONFIGS" | while IFS=: read -r label dir xbuild xgui xcuda xomp; do
  [ -n "$label" ] || continue
  log="$OUTDIR/${label}.log"
  {
    echo "############################################################"
    echo "### $label   ($dir)"
    echo "############################################################"
    date -u +"start: %Y-%m-%dT%H:%M:%SZ"
  } >"$log"

  if [ ! -f "$dir/CMakeCache.txt" ]; then
    echo "  *** $dir has no CMakeCache.txt. These qual-* configurations are CACHE-ONLY: a fresh" >>"$log"
    echo "  *** configure would silently produce Debug with OpenMP off. NOT TESTED." >>"$log"
    echo "$label: NOT AUTHORITATIVE (missing cache)" >>"$SUMMARY"
    continue
  fi

  # configure, then read the cache AFTER configuring (P18.7 fixed the reverse order as a defect)
  cmake -S . -B "$dir" >"$OUTDIR/${label}_configure.log" 2>&1
  echo "  configure exit: $?" >>"$log"

  gotb=$(grep -E '^CMAKE_BUILD_TYPE:' "$dir/CMakeCache.txt" | cut -d= -f2)
  gotg=$(grep -E '^CFDAPP_BUILD_GUI:' "$dir/CMakeCache.txt" | cut -d= -f2)
  gotc=$(grep -E '^CFDAPP_ENABLE_CUDA:' "$dir/CMakeCache.txt" | cut -d= -f2)
  goto=$(grep -E '^CFDAPP_ENABLE_OPENMP:' "$dir/CMakeCache.txt" | cut -d= -f2)
  printf '  CONFIG ASSERT  build=%-8s gui=%-4s cuda=%-4s openmp=%-4s\n' \
    "$gotb" "$gotg" "$gotc" "$goto" >>"$log"
  bad=0
  [ "$gotb" = "$xbuild" ] || { echo "  *** BUILD_TYPE is $gotb, expected $xbuild ***" >>"$log"; bad=1; }
  [ "$gotg" = "$xgui" ]   || { echo "  *** BUILD_GUI is $gotg, expected $xgui ***" >>"$log"; bad=1; }
  [ "$gotc" = "$xcuda" ]  || { echo "  *** ENABLE_CUDA is $gotc, expected $xcuda ***" >>"$log"; bad=1; }
  [ "$goto" = "$xomp" ]   || { echo "  *** ENABLE_OPENMP is $goto, expected $xomp ***" >>"$log"; bad=1; }
  if [ "$bad" -ne 0 ]; then
    echo "  *** CONFIGURATION MISMATCH -- FAIL CLOSED, this configuration is not tested ***" >>"$log"
    echo "$label: NOT AUTHORITATIVE (configuration mismatch)" >>"$SUMMARY"
    continue
  fi

  t0=$(date +%s)
  cmake --build "$dir" --clean-first -j "$BUILD_JOBS" >"$OUTDIR/${label}_build.log" 2>&1
  brc=$?
  build_elapsed=$(( $(date +%s) - t0 ))
  echo "  build exit: $brc   elapsed: ${build_elapsed} s   (clean-first)" >>"$log"
  if [ "$brc" -ne 0 ]; then
    echo "  *** BUILD FAILED -- not tested ***" >>"$log"
    tail -40 "$OUTDIR/${label}_build.log" >>"$log"
    echo "$label: NOT AUTHORITATIVE (build failed, exit $brc)" >>"$SUMMARY"
    continue
  fi

  # staleness: every test binary must be newer than the newest production source
  newest="$(newest_production | cut -d' ' -f2-)"
  stale=0
  echo "  --- staleness check against $newest ---" >>"$log"
  bins=$(find "$dir" -type f -name 'CFD*Tests' -perm -u+x 2>/dev/null | sort)
  [ -n "$bins" ] || { echo "    NO TEST BINARIES FOUND" >>"$log"; stale=1; }
  for b in $bins; do
    if [ "$b" -nt "$newest" ]; then echo "    OK    $b" >>"$log"
    else echo "    STALE $b" >>"$log"; stale=1; fi
  done
  if [ "$stale" -ne 0 ]; then
    echo "  *** STALE BINARY -- not authoritative ***" >>"$log"
    echo "$label: NOT AUTHORITATIVE (stale binary)" >>"$SUMMARY"
    continue
  fi

  {
    echo "  --- library and test-binary hashes ---"
    find "$dir" -type f \( -name 'libcfd*.a' -o -name 'CFD*Tests' -perm -u+x \) 2>/dev/null \
      | sort | xargs sha256sum 2>/dev/null | sed 's/^/    /'
    echo "  --- registered and excluded ---"
  } >>"$log"
  reg=$(ctest --test-dir "$dir" -N 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  exc=$(ctest --test-dir "$dir" -N -R "$EXCLUDE" 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  echo "    registered : ${reg:-?}" >>"$log"
  echo "    excluded   : ${exc:-?}  by the 5 frozen patterns" >>"$log"

  echo "  --- OMP_NUM_THREADS=1 ctest -j $CTEST_JOBS --timeout 7200 -E <5 frozen exclusions> ---" >>"$log"
  t1=$(date +%s)
  OMP_NUM_THREADS=1 ctest --test-dir "$dir" -j "$CTEST_JOBS" --timeout 7200 \
    -E "$EXCLUDE" --output-on-failure >"$OUTDIR/${label}_ctest.log" 2>&1
  crc=$?
  test_elapsed=$(( $(date +%s) - t1 ))

  C="$OUTDIR/${label}_ctest.log"
  passed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*   Passed' "$C")
  failed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*(Failed|Timeout|Exception)' "$C")
  skipped=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*Skipped' "$C")
  # ctest writes 'Not Run (Disabled)'. Grepping '(Not Run)' instead left 49 DISABLED_ tests
  # uncounted in every P17 phase; P18.7 fixed it and that fix is kept here.
  disabled=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*Not Run \(Disabled\)' "$C")
  counted=$(grep -aoE 'out of [0-9]+' "$C" | tail -1 | grep -oE '[0-9]+')

  {
    echo "  ctest exit status : $crc"
    echo "  ran ('out of')    : ${counted:-?}"
    echo "    Passed          : $passed"
    echo "    Failed          : $failed"
    echo "    Skipped         : $skipped"
    echo "    Disabled        : $disabled"
    echo "  ctest elapsed     : ${test_elapsed} s"
    echo "  Total Test time   : $(grep -aoE 'Total Test time \(real\) = [0-9.]+ sec' "$C" | tail -1)"
    echo ""
    echo "  --- every failing row, named ---"
    grep -aE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*(Failed|Timeout|Exception)' "$C" | sed 's/^/    /'
    echo ""
    echo "  --- P18.8's own rows ---"
    grep -aE '^ *[0-9]+/[0-9]+ Test +#.*P188' "$C" | sed 's/^/    /'
    date -u +"end: %Y-%m-%dT%H:%M:%SZ"
  } >>"$log"

  {
    echo "$label:  registered ${reg:-?}  ran ${counted:-?}  passed $passed  failed $failed  skipped $skipped  disabled $disabled"
    echo "    build ${build_elapsed} s   ctest ${test_elapsed} s   ctest exit $crc"
  } >>"$SUMMARY"
done

{
  echo ""
  echo "finished (WSL): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "newest production source at end: $(newest_production)"
  echo ""
  echo "NOTE: a nonzero ctest exit is EXPECTED for debug_gui, which has 3 known failures."
  echo "Whether those are the known ones is decided by comparing the named rows above against"
  echo "P18.7's recorded baseline, never by the exit code."
} >>"$SUMMARY"

exit "$overall"

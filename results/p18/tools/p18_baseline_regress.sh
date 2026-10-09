#!/usr/bin/env bash
# P18.0 section 3 -- the BASELINE regression, all three configurations, run BEFORE any P18 work.
#
# EXECUTION POLICY: inherited VERBATIM from P17.10, which inherited it from P13's G10 closeout:
#   OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200, the same 5 exclusion patterns, unchanged.
#
# TWO REPORTING DEFECTS OF THE INHERITED HARNESS ARE FIXED HERE. Both are recorded in
# results/p17/cpu_gpu_equivalence/HARNESS_FINDINGS.md, which named P17.11 as the place to fix them:
#   (1) it printed "Not Run : 0" by grepping '\(Not Run\)' while ctest writes 'Not Run (Disabled)'.
#       49 DISABLED_ tests have gone uncounted in every P17 phase. Now counted.
#   (2) it printed the CMake cache BEFORE configuring, so a fresh tree logged a grep error and
#       carried on in the wrong build type. The cache is now read AFTER configure and the four
#       qualification options are ASSERTED, failing closed on a mismatch.
# Neither change relaxes a threshold. Both make a number truthful that was not.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
Q=$ROOT/results/p18/baseline
L=$Q/logs
cd "$ROOT" || exit 1
mkdir -p "$L"   # self-sufficient: the first run failed because this did not exist
export PATH="/usr/local/cuda-12.9/bin:$PATH"

EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'

NEWEST_SRC_T=0
NEWEST_SRC_F=""
while IFS= read -r f; do
  t=$(stat -c %Y "$f")
  if [ "$t" -gt "$NEWEST_SRC_T" ]; then NEWEST_SRC_T=$t; NEWEST_SRC_F=$f; fi
done < <(find include src cuda -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cu' \
           -o -name '*.cuh' -o -name 'CMakeLists.txt' \) 2>/dev/null)

run_cfg() {  # label dir expect_buildtype expect_gui expect_cuda expect_openmp
  local label="$1" dir="$2" xbuild="$3" xgui="$4" xcuda="$5" xomp="$6"
  echo "############################################################"
  echo "### $label   ($dir)"
  echo "############################################################"
  cmake -S . -B "$dir" > "$L/B0_${label}_configure.log" 2>&1
  echo "  configure exit: $?"

  local gotb gotg gotc goto bad=0
  gotb=$(grep -E '^CMAKE_BUILD_TYPE:' "$dir/CMakeCache.txt" | cut -d= -f2)
  gotg=$(grep -E '^CFDAPP_BUILD_GUI:' "$dir/CMakeCache.txt" | cut -d= -f2)
  gotc=$(grep -E '^CFDAPP_ENABLE_CUDA:' "$dir/CMakeCache.txt" | cut -d= -f2)
  goto=$(grep -E '^CFDAPP_ENABLE_OPENMP:' "$dir/CMakeCache.txt" | cut -d= -f2)
  printf '  CONFIG ASSERT  build=%-8s gui=%-4s cuda=%-4s openmp=%-4s\n' "$gotb" "$gotg" "$gotc" "$goto"
  if [ "$gotb" != "$xbuild" ]; then echo "  *** BUILD_TYPE is $gotb, expected $xbuild ***"; bad=1; fi
  if [ "$gotg" != "$xgui" ]; then echo "  *** BUILD_GUI is $gotg, expected $xgui ***"; bad=1; fi
  if [ "$gotc" != "$xcuda" ]; then echo "  *** ENABLE_CUDA is $gotc, expected $xcuda ***"; bad=1; fi
  if [ "$goto" != "$xomp" ]; then echo "  *** ENABLE_OPENMP is $goto, expected $xomp ***"; bad=1; fi
  if [ "$bad" -ne 0 ]; then
    echo "  *** CONFIGURATION MISMATCH -- FAIL CLOSED, this configuration is not tested ***"
    echo ""
    return 1
  fi

  local t0 brc
  t0=$(date +%s)
  cmake --build "$dir" -j 16 > "$L/B0_${label}_build.log" 2>&1
  brc=$?
  echo "  build exit: $brc   ($(( $(date +%s) - t0 )) s)"
  if [ "$brc" -ne 0 ]; then
    grep -aE '(^|[^a-zA-Z])error:' "$L/B0_${label}_build.log" | head -10 | sed 's/^/    /'
    echo "  *** BUILD FAILED -- FAIL CLOSED ***"
    echo ""
    return 1
  fi

  echo "  --- staleness (P17.7 staleness.py, unchanged; fail closed) ---"
  ctest --test-dir "$dir" --show-only=json-v1 2>/dev/null \
    | python3 "$ROOT/results/p17/qualification/tools/list_test_bins.py" > "$L/B0_${label}_bins.txt"
  python3 "$ROOT/results/p17/hllc/tools/staleness.py" "$dir" "$ROOT" "$NEWEST_SRC_T" \
    < "$L/B0_${label}_bins.txt" > "$L/B0_${label}_staleness.log" 2>&1
  local strc=$?
  tail -6 "$L/B0_${label}_staleness.log" | sed 's/^/    /'
  echo "    staleness exit: $strc"

  echo "  --- library hashes ---"
  local lib
  for lib in "$dir/src/libcfdcore.a" "$dir/cuda/libcfdcuda.a"; do
    if [ -f "$lib" ]; then
      printf '    %-30s %s\n' "$(basename "$lib")" "$(sha256sum "$lib" | awk '{print $1}')"
    fi
  done

  local reg exc
  reg=$(ctest --test-dir "$dir" -N 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  exc=$(ctest --test-dir "$dir" -N -R "$EXCLUDE" 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  echo "  registered              : ${reg:-?}"
  echo "  matching the exclusions : ${exc:-?}"
  echo "  selected                : $(( ${reg:-0} - ${exc:-0} ))"

  echo "  --- OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 frozen exclusions> ---"
  local crc secs
  t0=$(date +%s)
  OMP_NUM_THREADS=1 ctest --test-dir "$dir" -j 8 --timeout 7200 -E "$EXCLUDE" --output-on-failure \
    > "$L/B0_${label}_ctest.log" 2>&1
  crc=$?
  secs=$(( $(date +%s) - t0 ))

  local passed failed skipped disabled counted
  passed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*   Passed' "$L/B0_${label}_ctest.log")
  failed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*(Failed|Timeout|Exception)' "$L/B0_${label}_ctest.log")
  skipped=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*Skipped' "$L/B0_${label}_ctest.log")
  disabled=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*Not Run \(Disabled\)' "$L/B0_${label}_ctest.log")
  counted=$(grep -aoE 'out of [0-9]+' "$L/B0_${label}_ctest.log" | tail -1 | grep -oE '[0-9]+')
  echo "  ctest exit status       : $crc"
  echo "  result lines            : $(( passed + failed + skipped + disabled ))"
  echo "    Passed                : $passed"
  echo "    FAILED                : $failed"
  echo "    Skipped               : $skipped"
  echo "    Not Run (DISABLED)    : $disabled"
  echo "  ctest 'out of'          : ${counted:-?}"
  echo "  runtime                 : ${secs} s"
  if [ "$failed" -ne 0 ] || [ "$crc" -ne 0 ]; then
    echo "  --- every non-passing test ---"
    grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/B0_${label}_ctest.log" | head -40 | sed 's/^/    /'
  fi
  echo ""
  return 0
}

log=$L/B0_p18_baseline_regress.log
{
echo "P18.0 -- BASELINE regression, three configurations"
echo "date: $(date -u +%FT%TZ)"
echo "P18 start fingerprint: 60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318"
echo "newest production source: $(date -u -d @"$NEWEST_SRC_T" +%FT%TZ)  $NEWEST_SRC_F"
echo ""
echo "### PREDICTIONS -- the P17.11 MEASURED baseline, which P18.0 must reproduce ###"
echo "  release_cuda  2763 selected, 2712 Passed, 0 FAILED, 2 Skipped, 49 DISABLED"
echo "  cpu_only      2540 selected, 2493 Passed, 0 FAILED, 0 Skipped, 47 DISABLED"
echo "  debug_gui     2825 selected, 2770 Passed, 4 FAILED, 2 Skipped, 49 DISABLED"
echo ""
echo "  THE 4 DEBUG+GUI FAILURES ARE EXPECTED AND INHERITED, not a P18 regression:"
echo "    DriverSeegmiller.MeshSequence, .CorrectedSequenceIsDeterministic,"
echo "    DriverSeegmillerInlet.MappedInletPreservesTheMassFlow, .MediumGridAB"
echo "  all four TIMEOUT at the frozen 7200 s. They pass in both Release configurations and are"
echo "  pre-P17 tests; Debug is >3.8x-6.0x slower than the limit was set for. P17.11 left G10 at"
echo "  PARTIAL for exactly this. The timeout is NOT raised here either."
echo "  results/p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md"
echo ""
echo "  A DEVIATION FROM ANY OF THESE NUMBERS IS A P18.0 FINDING. Reproducing them -- including the"
echo "  four failures -- is what establishes the baseline."
echo ""
run_cfg release_cuda "$ROOT/build/qual-final"   Release OFF ON  ON
run_cfg cpu_only     "$ROOT/build/qual-cpuonly" Release OFF OFF ON
run_cfg debug_gui    "$ROOT/build/qual-debug"   Debug   ON  ON  OFF
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

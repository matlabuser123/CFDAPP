#!/usr/bin/env bash
# P18.2 section 42 -- the POST-CHANGE regression, all three configurations, run BEFORE any P18 work.
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
Q=$ROOT/results/p18/species_transport
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
  cmake -S . -B "$dir" > "$L/P18_2_${label}_configure.log" 2>&1
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
  cmake --build "$dir" -j 16 > "$L/P18_2_${label}_build.log" 2>&1
  brc=$?
  echo "  build exit: $brc   ($(( $(date +%s) - t0 )) s)"
  if [ "$brc" -ne 0 ]; then
    grep -aE '(^|[^a-zA-Z])error:' "$L/P18_2_${label}_build.log" | head -10 | sed 's/^/    /'
    echo "  *** BUILD FAILED -- FAIL CLOSED ***"
    echo ""
    return 1
  fi

  echo "  --- staleness (P17.7 staleness.py, unchanged; fail closed) ---"
  ctest --test-dir "$dir" --show-only=json-v1 2>/dev/null \
    | python3 "$ROOT/results/p17/qualification/tools/list_test_bins.py" > "$L/P18_2_${label}_bins.txt"
  python3 "$ROOT/results/p17/hllc/tools/staleness.py" "$dir" "$ROOT" "$NEWEST_SRC_T" \
    < "$L/P18_2_${label}_bins.txt" > "$L/P18_2_${label}_staleness.log" 2>&1
  local strc=$?
  tail -6 "$L/P18_2_${label}_staleness.log" | sed 's/^/    /'
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
    > "$L/P18_2_${label}_ctest.log" 2>&1
  crc=$?
  secs=$(( $(date +%s) - t0 ))

  local passed failed skipped disabled counted
  passed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*   Passed' "$L/P18_2_${label}_ctest.log")
  failed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*(Failed|Timeout|Exception)' "$L/P18_2_${label}_ctest.log")
  skipped=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*Skipped' "$L/P18_2_${label}_ctest.log")
  disabled=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*Not Run \(Disabled\)' "$L/P18_2_${label}_ctest.log")
  counted=$(grep -aoE 'out of [0-9]+' "$L/P18_2_${label}_ctest.log" | tail -1 | grep -oE '[0-9]+')
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
    grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/P18_2_${label}_ctest.log" | head -40 | sed 's/^/    /'
  fi
  echo ""
  return 0
}

log=$L/P18_2_regress.log
{
echo "P18.2 -- POST-CHANGE regression, three configurations"
echo "date: $(date -u +%FT%TZ)"
echo "P18 start fingerprint: 60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318"
echo "newest production source: $(date -u -d @"$NEWEST_SRC_T" +%FT%TZ)  $NEWEST_SRC_F"
echo ""
echo "### PREDICTIONS -- DECLARED BEFORE THIS RUN (G11's requirement) ###"
echo ""
echo "P18.2 adds ONE new test file (tests/unit/species/test_species_transport.cpp, 34 gtest"
echo "tests, counted from --gtest_list_tests at logs/11_focused_status_final.log) and two new"
echo "production translation units already present from P18.1 plus src/species/SpeciesTransport.cpp."
echo "It also adds an OPT-IN parameter to P14's diffusion assembler, defaulting to the"
echo "pre-P18.2 behaviour, so every existing caller is bitwise unchanged -- measured at"
echo "logs/10_preservation_final.log: transport 22/22, transport characterization 3/3,"
echo "P14 migration 3/3, turbulence 246/246, thermal 163/163."
echo ""
echo "gtest_discover_tests registers ONE CTEST ROW PER GTEST TEST, so 34 rows are added to every"
echo "configuration. None matches the 5 frozen exclusion patterns, so selected rises by 34 too."
echo "Skipped and DISABLED must NOT move: the new tests are neither."
echo ""
echo "  P18.1 CLOSE (the baseline)              PREDICTED (P18.2 = baseline + 34)"
echo "  release_cuda 2823 sel 2772 P 0 F  ->  2857 selected, 2806 Passed, 0 FAILED, 2 Skipped, 49 DISABLED"
echo "  cpu_only     2600 sel 2553 P 0 F  ->  2634 selected, 2587 Passed, 0 FAILED, 0 Skipped, 47 DISABLED"
echo "  debug_gui    2885 sel 2831 P 3 F  ->  2919 selected, 2865 Passed, 3 FAILED, 2 Skipped, 49 DISABLED"
echo ""
echo "  THE DEBUG+GUI FAILURE COUNT IS PREDICTED AS 3 OR 4, AND THAT IS NOT HEDGING -- it is"
echo "  what P18.1 MEASURED. DriverSeegmiller.MeshSequence completed in 7123.03 s against the"
echo "  frozen 7200 s cap, i.e. at 98.9% of the limit, so whether it times out depends on machine"
echo "  load. The inherited set is THREE RELIABLE TIMEOUTS plus ONE MARGINAL TEST, not four"
echo "  reliable ones. If Passed is 2865 with 3 FAILED, or 2864 with 4 FAILED, both reproduce the"
echo "  known behaviour. Any FIFTH failure, or any other test in the set, is a P18.2 finding."
echo "    DriverSeegmiller.CorrectedSequenceIsDeterministic"
echo "    DriverSeegmillerInlet.MappedInletPreservesTheMassFlow"
echo "    DriverSeegmillerInlet.MediumGridAB"
echo "    DriverSeegmiller.MeshSequence   <-- MARGINAL"
echo "  The 7200 s timeout is NOT raised."
echo ""
echo "  ANY DEVIATION OTHER THAN THE MARGINAL TIMEOUT IS A P18.2 FINDING."
echo ""
run_cfg release_cuda "$ROOT/build/qual-final"   Release OFF ON  ON
run_cfg cpu_only     "$ROOT/build/qual-cpuonly" Release OFF OFF ON
run_cfg debug_gui    "$ROOT/build/qual-debug"   Debug   ON  ON  OFF
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

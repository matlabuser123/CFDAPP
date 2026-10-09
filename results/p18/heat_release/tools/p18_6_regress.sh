#!/usr/bin/env bash
# P18.6 section 59 -- the POST-CHANGE regression, all three configurations.
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
Q=$ROOT/results/p18/heat_release
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
  cmake -S . -B "$dir" > "$L/P18_6_${label}_configure.log" 2>&1
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
  cmake --build "$dir" -j 16 > "$L/P18_6_${label}_build.log" 2>&1
  brc=$?
  echo "  build exit: $brc   ($(( $(date +%s) - t0 )) s)"
  if [ "$brc" -ne 0 ]; then
    grep -aE '(^|[^a-zA-Z])error:' "$L/P18_6_${label}_build.log" | head -10 | sed 's/^/    /'
    echo "  *** BUILD FAILED -- FAIL CLOSED ***"
    echo ""
    return 1
  fi

  echo "  --- staleness (P17.7 staleness.py, unchanged; fail closed) ---"
  ctest --test-dir "$dir" --show-only=json-v1 2>/dev/null \
    | python3 "$ROOT/results/p17/qualification/tools/list_test_bins.py" > "$L/P18_6_${label}_bins.txt"
  python3 "$ROOT/results/p17/hllc/tools/staleness.py" "$dir" "$ROOT" "$NEWEST_SRC_T" \
    < "$L/P18_6_${label}_bins.txt" > "$L/P18_6_${label}_staleness.log" 2>&1
  local strc=$?
  tail -6 "$L/P18_6_${label}_staleness.log" | sed 's/^/    /'
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
    > "$L/P18_6_${label}_ctest.log" 2>&1
  crc=$?
  secs=$(( $(date +%s) - t0 ))

  local passed failed skipped disabled counted
  passed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*   Passed' "$L/P18_6_${label}_ctest.log")
  failed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*(Failed|Timeout|Exception)' "$L/P18_6_${label}_ctest.log")
  skipped=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*Skipped' "$L/P18_6_${label}_ctest.log")
  disabled=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*Not Run \(Disabled\)' "$L/P18_6_${label}_ctest.log")
  counted=$(grep -aoE 'out of [0-9]+' "$L/P18_6_${label}_ctest.log" | tail -1 | grep -oE '[0-9]+')
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
    grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/P18_6_${label}_ctest.log" | head -40 | sed 's/^/    /'
  fi
  echo ""
  return 0
}

log=$L/P18_6_regress.log
{
echo "P18.6 -- POST-CHANGE regression, three configurations"
echo "date: $(date -u +%FT%TZ)"
echo "P18 start fingerprint: 60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318"
echo "newest production source: $(date -u -d @"$NEWEST_SRC_T" +%FT%TZ)  $NEWEST_SRC_F"
echo ""
echo "### PREDICTIONS -- DECLARED BEFORE THIS RUN ###"
echo ""
echo "P18.6 adds ONE test file (tests/unit/species/test_heat_release.cpp, 42 gtest tests:"
echo "4 FormationEnthalpy + 7 ReactionEnthalpy + 11 HeatSource + 4 EnergyAccounting +"
echo "9 ClosedReactor + 3 TemperatureFeedback + 2 NonReactingLimit + 2 HeatReleaseDeterminism,"
echo "counted from --gtest_list_tests) and ONE production translation unit"
echo "(src/species/HeatRelease.cpp)."
echo ""
echo "P18.6 MODIFIES NO EXISTING PRODUCTION SOURCE. The only changes to pre-existing files are"
echo "two build-list additions. P18.6 adds NO energy equation -- the W/m^3 source is consumed by"
echo "the P15 per-cell heat-source assembler that has existed since P12-NUM-006 -- and does NOT"
echo "lift P17 TotalEnergyState refusal of a temperature-dependent cp."
echo "Measured at logs/05_preservation.log: P18.1 60/60, P18.2 34/34, P18.3 34/34, P18.4 38/38,"
echo "P18.5 38/38, P18.6 42/42, pre-P18 37/37, and 60+34+34+38+38+42+37 = 283 = the whole"
echo "binary, so the groups partition it with nothing uncounted."
echo "P15 thermal 163/163 and P17 compressible 215/215 are unchanged (logs/00 baseline)."
echo ""
echo "gtest_discover_tests registers ONE CTEST ROW PER GTEST TEST, so 42 rows are added to every"
echo "configuration. None matches the 5 frozen exclusion patterns, so selected rises by 42 too."
echo "Skipped and DISABLED must NOT move -- P18.6 adds no DISABLED_ test and no skip."
echo ""
echo "  P18.5 CLOSE (the baseline)              PREDICTED (P18.6 = baseline + 42)"
echo "  release_cuda 2967 sel 2916 P 0 F  ->  3009 selected, 2958 Passed, 0 FAILED, 2 Skipped, 49 DISABLED"
echo "  cpu_only     2744 sel 2697 P 0 F  ->  2786 selected, 2739 Passed, 0 FAILED, 0 Skipped, 47 DISABLED"
echo "  debug_gui    3029 sel 2974 P 4 F  ->  3071 selected, 3016 Passed, 4 FAILED, 2 Skipped, 49 DISABLED"
echo ""
echo "  DEBUG+GUI FAILURES PREDICTED AS 3 OR 4. DriverSeegmiller.MeshSequence is MARGINAL against"
echo "  the frozen 7200 s cap and is now measured FIVE times:"
echo "      P18.1  PASSED   7123.03 s   98.9%"
echo "      P18.2  TIMEOUT  7201.94 s  >100%"
echo "      P18.3  PASSED   6764.75 s   94.0%"
echo "      P18.4  PASSED   7034.57 s   97.7%"
echo "      P18.5  TIMEOUT  7200.12 s  >100%"
echo "  So 3016 Passed with 4 FAILED, or 3017 with 3, both reproduce the known behaviour."
echo "  The reliable three are CorrectedSequenceIsDeterministic, MappedInletPreservesTheMassFlow"
echo "  and MediumGridAB. THE 7200 s TIMEOUT IS NOT RAISED."
echo ""
echo "  ANY DEVIATION OTHER THAN THE MARGINAL TIMEOUT IS A P18.6 FINDING."
echo ""
run_cfg release_cuda "$ROOT/build/qual-final"   Release OFF ON  ON
run_cfg cpu_only     "$ROOT/build/qual-cpuonly" Release OFF OFF ON
run_cfg debug_gui    "$ROOT/build/qual-debug"   Debug   ON  ON  OFF
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

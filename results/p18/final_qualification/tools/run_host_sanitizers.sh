#!/usr/bin/env bash
# P18.9 section 44 -- HOST SANITIZER COVERAGE (ASan + UBSan).
#
# WHY THIS EXISTS AT ALL: no P18 phase has a sanitizer log. P18.1's own regression.md named the gap
# and refused to paper over it -- "There is correspondingly little for ASan/UBSan to find here, but
# that is an argument for expecting a clean run, NOT a substitute for one." P18.9 is where that run
# happens.
#
# SETTINGS ARE CLAUDE.md §7's, verbatim, not invented here:
#     ctest --timeout 7200
#     ASAN_OPTIONS=detect_leaks=1:halt_on_error=0
#     UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
# plus the execution policy P18.8 learned to inherit: OMP_NUM_THREADS=1, -j 8, and the same 5 frozen
# exclusion patterns. A sanitizer run under different concurrency than the baseline would inflate
# the long tests exactly as P18.8's first regression attempt did.
#
# NOTE ON SCOPE, measured rather than assumed: build/asan is Debug with CUDA OFF, so no CUDA kernel
# is exercised here. CUDA is covered separately by compute-sanitizer (section 43). Tests that
# already sit at or near the 7200 s cap in PLAIN Debug cannot complete under ASan's 2-3x slowdown;
# they are left in the run so their timeouts are MEASURED and classified, not silently excluded.
#
# Usage:  run_host_sanitizers.sh <output-directory>
set -u

OUTDIR="${1:?usage: run_host_sanitizers.sh <output-directory>}"
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1
mkdir -p "$OUTDIR"

DIR=build/asan
EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'
LOG="$OUTDIR/host_sanitizers.log"

{
  echo "============================================================"
  echo "P18.9 section 44 -- HOST SANITIZERS (ASan + UBSan)"
  echo "============================================================"
  echo "started (WSL): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "git HEAD: $(git rev-parse HEAD)"
  echo ""
} >"$LOG"

if [ ! -f "$DIR/CMakeCache.txt" ]; then
  echo "NOT RUN: $DIR has no CMakeCache.txt" >>"$LOG"
  exit 1
fi

{
  echo "--- the configuration, read from its own cache ---"
  for k in CMAKE_BUILD_TYPE CFDAPP_BUILD_GUI CFDAPP_ENABLE_CUDA CFDAPP_ENABLE_OPENMP \
           CFDAPP_ENABLE_SANITIZERS; do
    v=$(grep -E "^$k:" "$DIR/CMakeCache.txt" | cut -d= -f2)
    printf '    %-28s %s\n' "$k" "${v:-UNSET}"
  done
  echo ""
  echo "--- are the sanitizers actually in the compile line? (not just the cache flag) ---"
  s=$(ninja -C "$DIR" -t commands 2>/dev/null | grep -oE '\-fsanitize=[a-z,]+' | sort -u | tr '\n' ' ')
  echo "    observed -fsanitize flags: ${s:-NONE FOUND}"
  if [ -z "$s" ]; then
    echo "    *** NO -fsanitize IN THE COMPILE LINE. A clean run would prove nothing. ***"
  fi
  echo ""
} >>"$LOG"

echo "--- clean-first build (ASan, Debug, CUDA OFF) ---" >>"$LOG"
t0=$(date +%s)
cmake --build "$DIR" --clean-first -j 16 >"$OUTDIR/asan_build.log" 2>&1
brc=$?
{
  echo "    build exit: $brc   elapsed: $(( $(date +%s) - t0 )) s"
  tail -5 "$OUTDIR/asan_build.log" | sed 's/^/      /'
} >>"$LOG"
if [ "$brc" -ne 0 ]; then
  echo "*** BUILD FAILED -- not run ***" >>"$LOG"
  exit 1
fi

{
  echo ""
  echo "--- registered / excluded ---"
  reg=$(ctest --test-dir "$DIR" -N 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  exc=$(ctest --test-dir "$DIR" -N -R "$EXCLUDE" 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  echo "    registered : ${reg:-?}"
  echo "    excluded   : ${exc:-?}  by the 5 frozen patterns"
  echo ""
  echo "--- ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 ---"
  echo "--- UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 ---"
  echo "--- OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 frozen> ---"
} >>"$LOG"

t1=$(date +%s)
OMP_NUM_THREADS=1 \
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
  ctest --test-dir "$DIR" -j 8 --timeout 7200 -E "$EXCLUDE" --output-on-failure \
  >"$OUTDIR/asan_ctest.log" 2>&1
crc=$?
elapsed=$(( $(date +%s) - t1 ))

C="$OUTDIR/asan_ctest.log"
passed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*   Passed' "$C")
failed=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*(Failed|Timeout|Exception)' "$C")
skipped=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*Skipped' "$C")
disabled=$(grep -acE '^ *[0-9]+/[0-9]+ Test +#.*Not Run \(Disabled\)' "$C")
counted=$(grep -aoE 'out of [0-9]+' "$C" | tail -1 | grep -oE '[0-9]+')

{
  echo ""
  echo "============ RESULT ============"
  echo "  ctest exit     : $crc"
  echo "  ran ('out of') : ${counted:-?}"
  echo "    Passed       : $passed"
  echo "    Failed       : $failed"
  echo "    Skipped      : $skipped"
  echo "    Disabled     : $disabled"
  echo "  elapsed        : ${elapsed} s"
  echo ""
  echo "============ SANITIZER FINDINGS ============"
  echo "  AddressSanitizer reports : $(grep -ac 'ERROR: AddressSanitizer' "$C")"
  echo "  LeakSanitizer reports    : $(grep -ac 'ERROR: LeakSanitizer\|Direct leak\|Indirect leak' "$C")"
  echo "  UBSan reports            : $(grep -ac 'runtime error:' "$C")"
  echo ""
  echo "  --- every distinct AddressSanitizer error kind ---"
  grep -aoE 'ERROR: AddressSanitizer: [a-z-]+' "$C" | sort | uniq -c | sed 's/^/    /'
  echo "  --- every distinct UBSan message ---"
  grep -aoE 'runtime error: [^(]*' "$C" | sort | uniq -c | sort -rn | head -20 | sed 's/^/    /'
  echo ""
  echo "  --- every non-passing row, named ---"
  grep -aE '^ *[0-9]+/[0-9]+ Test +#.*\*\*\*(Failed|Timeout|Exception)' "$C" | sed 's/^/    /'
  echo ""
  echo "  --- P18's own rows under the sanitizers ---"
  grep -aE '^ *[0-9]+/[0-9]+ Test +#.*(Species|Reaction|Arrhenius|Heat|Mixture|MassFraction|P187|P188)' "$C" \
    | sed 's/^/    /' | head -70
  echo ""
  echo "finished (WSL): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >>"$LOG"

exit 0

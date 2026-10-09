#!/usr/bin/env bash
# P17.7 regression. Execution policy FROZEN, copied verbatim from
# results/p13/final-qualification/2026-09-24/g10-closeout/tools/G10_regress.sh.
# Gate: results/p17/hllc/acceptance_gate.md, frozen BEFORE this ran,
#       sha256 1f4ccd8af57f529e1654b2465df9e1c07124c22e0e8a500c56116372009a6465
# RECOMPUTED and compared at run time: the constant this script inherited (ded6feaa...)
# belonged to an earlier phase's gate, so it could never have detected drift.
#
# Adds to G10's harness, per CLAUDE.md section 8, an explicit FAIL-CLOSED staleness
# check: every test binary must be newer than the newest production source. G10
# relied on the build step alone; this verifies the property directly.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
OUT=$ROOT/results/p17/hllc
L=$OUT/logs
cd "$ROOT" || exit 1
mkdir -p "$L"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'

DOCEXC=(. ':(exclude)results/' ':(exclude)TODO.md' ':(exclude)ROADMAP.md' ':(exclude)CLAUDE.md'
        ':(exclude)README.md' ':(exclude)CONTRIBUTING.md' ':(exclude)QUALITY_GATE.md'
        ':(exclude)PROJECT_STRUCTURE.md' ':(exclude)RELEASE_NOTES_*.md' ':(exclude)docs/')

code_fp() {
  local T U
  T=$(git diff --binary HEAD -- "${DOCEXC[@]}" | sha256sum | awk '{print $1}')
  U=$(git ls-files --others --exclude-standard -- "${DOCEXC[@]}" \
      | LC_ALL=C sort | while read -r f; do sha256sum "$f"; done | sha256sum | awk '{print $1}')
  printf '%s %s' "$T" "$U" | sha256sum | awk '{print $1}'
}

# The AUTHORITATIVE test-binary set: command[0] of every test ctest has registered,
# deduplicated. NOT a find(1) guess -- the repository sits on a /mnt/c drvfs mount
# that reports every file as mode 0777, so `find -executable` matches object files
# and vendored headers too. Attempt 1 failed closed on exactly that; see
# logs/PRESERVED_invalid_binary_discovery/WHY_INVALID.md.
find_test_bins() {   # find_test_bins <build dir>
  ctest --test-dir "$1" --show-only=json-v1 2>/dev/null | python3 -c '
import json, os, sys
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(3)
seen = set()
for t in d.get("tests", []):
    cmd = t.get("command") or []
    if not cmd:
        continue
    exe = cmd[0]
    if exe in seen:
        continue
    seen.add(exe)
    if os.path.isfile(exe):
        print(exe)
if not seen:
    sys.exit(4)
'
}

STAGE="${1:-mandated}"
log=$L/50_p17_7_regress_${STAGE}.log

{
echo "P17.7 -- regression   stage: $STAGE"
echo "date: $(date -u +%FT%TZ)"
echo "gate: results/p17/hllc/acceptance_gate.md"
GATE_FROZEN=1f4ccd8af57f529e1654b2465df9e1c07124c22e0e8a500c56116372009a6465
GATE_LIVE=$(sha256sum "$OUT/acceptance_gate.md" | awk '{print $1}')
echo "      frozen sha256 $GATE_FROZEN"
echo "      live   sha256 $GATE_LIVE"
if [ "$GATE_LIVE" = "$GATE_FROZEN" ]; then
  echo "      GATE INTEGRITY: MATCHES the freeze"
else
  echo "      *** GATE INTEGRITY: the gate MOVED since the freeze. Under a frozen-gate"
  echo "          protocol these results are not authoritative. FAILING CLOSED. ***"
  echo "done: $(date -u +%FT%TZ)"
  exit 1
fi
echo ""
echo "FROZEN exclusions (5 patterns, verbatim from G10_regress.sh):"
echo "  $EXCLUDE"
echo "FROZEN invocation: OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200   (debug_gui: 14400)"
echo "nvcc on PATH: $(command -v nvcc)"
nvcc --version 2>/dev/null | tail -2 | sed 's/^/  /'
echo ""
echo "P17_CODE_FINGERPRINT before : $(code_fp)"
echo "P17.6 close was          : cf4d53a2e6b090228f68d58bc9b452d324abd3dde03539a53a70f44542c70bdf (MOVES by the authorized P17.7 delta)"
echo ""

# The newest PRODUCTION source only -- include/ src/ cuda/. libcfdcore.a and
# libcfdcuda.a link into EVERY test binary, so a production edit invalidates all of
# them; that is the real hazard section 8 is about. A TEST source, by contrast, only
# affects the binary that compiles it, and is checked per-directory below.
# Attempt 1 used the whole tree and failed closed on 45 binaries that do not compile the
# one reformatted test file; see logs/PRESERVED_invalid_crossdir_staleness/WHY_INVALID.md.
NEWEST_SRC_T=0
NEWEST_SRC_F=""
while IFS= read -r f; do
  t=$(stat -c %Y "$f")
  if [ "$t" -gt "$NEWEST_SRC_T" ]; then NEWEST_SRC_T=$t; NEWEST_SRC_F=$f; fi
done < <(find include src cuda -type f \
           \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cu' \
              -o -name '*.cuh' -o -name 'CMakeLists.txt' \) 2>/dev/null)
echo "newest PRODUCTION source: $(date -u -d @"$NEWEST_SRC_T" +%FT%TZ)  $NEWEST_SRC_F"
echo "  (test sources are checked per-directory against their own binary, below)"
echo ""

run_cfg() {   # run_cfg <label> <build dir> <timeout> <pred registered> <pred executed>
  local label="$1" dir="$2" tmo="$3" pred_reg="$4" pred_exec="$5"
  echo "############################################################"
  echo "### $label  ($dir)   --timeout $tmo"
  echo "############################################################"
  grep -hE 'CMAKE_BUILD_TYPE:STRING|CFDAPP_ENABLE_CUDA:BOOL|CFDAPP_BUILD_GUI:BOOL|CFDAPP_ENABLE_OPENMP:BOOL' \
    "$dir/CMakeCache.txt" | sed 's/^/  /'
  echo ""
  echo "--- build (every target) ---"
  local t0 brc
  t0=$(date +%s)
  cmake --build "$dir" -j 16 > "$L/50_${label}_build.log" 2>&1
  brc=$?
  echo "  build exit: $brc   ($(( $(date +%s) - t0 )) s)"
  grep -aE '(^|[^a-zA-Z])error:|FAILED' "$L/50_${label}_build.log" | head -10 | sed 's/^/    /'
  if [ $brc -ne 0 ]; then
    echo "  *** BUILD FAILED -- FAIL CLOSED, this configuration is not tested ***"
    echo ""
    return 1
  fi

  echo ""
  echo "--- section 8 step 3: every BUILT test binary newer than the sources it ACTUALLY COMPILES (FAIL CLOSED) ---"
  # The property "demonstrably rebuilt against the exact source" applies to binaries THIS BUILD
  # PRODUCES. ctest's registry also names fixed external tools -- some CLI rejection tests are driven
  # by `cmake -P` -- and a 2024 /usr/bin/cmake is not stale, it is not ours. Both sets are printed, so
  # the exclusion is visible rather than silent. Attempt 2 of P17.0 failed closed on /usr/bin/cmake;
  # see logs/PRESERVED_invalid_system_binary/WHY_INVALID.md.
  #
  # The per-binary source set now comes from CMake's OWN OBJECT LIST under CMakeFiles/<target>.dir,
  # not from a glob of the binary's directory. The directory heuristic was wrong BOTH ways: it
  # falsely accused the untouched CFDLowMachRegressionTests of a SIBLING TARGET's new source
  # (P17.6 puts two test targets in one directory), and it never checked a source compiled in from
  # another directory at all. Attempt 1 of this phase failed closed on exactly that; preserved in
  # logs/PRESERVED_invalid_perdir_staleness/. The replacement is STRICTLY STRONGER.
  find_test_bins "$dir" > "$L/50_${label}_bins.txt"
  python3 "$OUT/tools/staleness.py" "$dir" "$ROOT" "$NEWEST_SRC_T" < "$L/50_${label}_bins.txt"
  local strc=$?
  if [ "$strc" -ne 0 ]; then
    echo "  *** STALE OR UNVERIFIABLE BINARIES -- FAIL CLOSED, results would not be authoritative ***"
    echo ""
    return 1
  fi

  echo ""
  echo "--- library and test-binary hashes (section 8 step 4) ---"
  local lib
  for lib in "$dir/libcfdcore.a" "$dir/src/libcfdcore.a" "$dir/cuda/libcfdcuda.a"; do
    if [ -f "$lib" ]; then
      printf '  %-46s %s\n' "${lib#"$dir"/}" "$(sha256sum "$lib" | awk '{print $1}')"
    fi
  done
  find_test_bins "$dir" | LC_ALL=C sort | while read -r b; do
    printf '  %-46s %s\n' "$(basename "$b")" "$(sha256sum "$b" | awk '{print $1}')"
  done

  echo ""
  echo "--- registered tests (ctest -N, nothing executed) ---"
  local reg exc_n eos_n
  reg=$(ctest --test-dir "$dir" -N 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  exc_n=$(ctest --test-dir "$dir" -N -R "$EXCLUDE" 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  eos_n=$(ctest --test-dir "$dir" -N -R 'IdealGasEOSTest' 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  echo "  registered              : ${reg:-unknown}   PREDICTED $pred_reg"
  echo "  matching the exclusions : ${exc_n:-unknown}   PREDICTED 13"
  echo "  IdealGasEOSTest rows    : ${eos_n:-unknown}   PREDICTED 30"
  ctest --test-dir "$dir" -N -R 'IdealGasEOSTest' 2>/dev/null | grep -aE 'Test +#' | sed 's/^/    /'

  echo ""
  echo "--- OMP_NUM_THREADS=1 ctest -j 8 --timeout $tmo -E <5 frozen exclusions> ---"
  local crc secs total pct failed notrun disabled
  t0=$(date +%s)
  OMP_NUM_THREADS=1 ctest --test-dir "$dir" -j 8 --timeout "$tmo" -E "$EXCLUDE" --output-on-failure \
    > "$L/50_${label}_ctest.log" 2>&1
  crc=$?
  secs=$(( $(date +%s) - t0 ))
  total=$(grep -aoE 'out of [0-9]+' "$L/50_${label}_ctest.log" | tail -1 | grep -oE '[0-9]+')
  pct=$(grep -aoE '[0-9]+% tests passed' "$L/50_${label}_ctest.log" | tail -1)
  failed=$(grep -aoE '[0-9]+ tests failed' "$L/50_${label}_ctest.log" | tail -1 | grep -oE '^[0-9]+')
  notrun=$(grep -acE '\(Not Run\)' "$L/50_${label}_ctest.log" || true)
  disabled=$(grep -acE '\(Disabled\)' "$L/50_${label}_ctest.log" || true)
  echo "  ctest exit status : $crc"
  echo "  total tests       : ${total:-unknown}      PREDICTED $pred_exec"
  echo "  passed            : ${pct:-unknown}"
  echo "  failed            : ${failed:-0}           PREDICTED 0"
  echo "  Not Run           : $notrun"
  echo "  Disabled (raw x2) : $disabled"
  echo "  runtime           : ${secs} s ($(awk -v s="$secs" 'BEGIN{printf "%.1f", s/60}') min)  [WSL wall clock, indicative only]"
  if [ "${total:-0}" = "$pred_exec" ]; then
    echo "  COUNT VERDICT     : MATCHES the frozen prediction"
  else
    echo "  COUNT VERDICT     : *** DEVIATION from the frozen prediction -- baseline_gate.md section 5 ***"
  fi
  if [ "${failed:-0}" != "0" ] || [ "$crc" -ne 0 ]; then
    echo "  --- every non-passing test ---"
    grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/50_${label}_ctest.log" | sed 's/^/    /'
  fi
  echo ""
  # The IdealGasEOSTest block below is P17.2's and is KEPT: it is still a live PRESERVATION
  # detector -- those rows must go on passing -- not an obsolete constant. But it says
  # nothing about P17.6, so this phase's own rows are enumerated too; otherwise the mandated
  # run could pass while P17.6's tests were registered and never executed.
  echo "--- B4: the P17.7 tests EXECUTED, by name (registration is not execution) ---"
  grep -aE 'Hllc[A-Za-z]*Test\.' "$L/50_${label}_ctest.log" \
    | grep -aE 'Passed|Failed' | sed 's/^/    /'
  echo "  P17.7 rows that Passed: $(grep -acE 'Hllc[A-Za-z]*Test\..*Passed' "$L/50_${label}_ctest.log" || true)   PREDICTED 39 release_cuda / 35 cpu_only"
  echo "  of which the two post-freeze additions (sections 26 and 16B, added after the gate froze):"
  grep -aE '(AddingAConstantVelocityTransformsTheFluxConsistently|ZeroVelocityUniformPressureCarriesOnlyPressure)' \
    "$L/50_${label}_ctest.log" | grep -aE 'Passed|Failed' | sed 's/^/    /'
  echo ""
  echo "--- B4 (P17.2 preservation): the IdealGasEOS tests EXECUTED, by name ---"
  grep -aE 'IdealGasEOSTest\.' "$L/50_${label}_ctest.log" | grep -aE 'Passed|Failed' | sed 's/^/    /'
  echo "  IdealGasEOSTest rows that Passed: $(grep -acE 'IdealGasEOSTest\..*Passed' "$L/50_${label}_ctest.log" || true)   PREDICTED 30"
  echo ""
}

if [ "$STAGE" = "mandated" ]; then
  # THE PREDICTION, declared BEFORE this runs.
  # P17.6's gate froze the baseline as the P17.5 CLOSE: Release+CUDA 2642 registered / 2580
  # executed, CPU-only 2440 / 2380. This phase adds 15 tests, all in the ONE new file
  # tests/integration/compressible/test_low_mach_family.cpp:
  #     2642 + 15 = 2657 registered      2580 + 15 = 2595 executed
  #     2440 + 15 = 2455 registered      2380 + 15 = 2395 executed
  # A test count is an arithmetic consequence of the suite, not a tolerance. P17.6 changes NO
  # production source -- one new test file and one CMake line -- so nothing else may move.
  #
  # THE IdealGasEOSTest CONSTANT IS CORRECTED HERE, to 30. It said 21 from P17.2 through P17.6 --
  # five phases of an obsolete constant on a live detector, reported as stale each time because
  # correcting it mid-phase would have edited a frozen prediction. This is a NEW script with a NEW
  # prediction, so the right moment to fix it is now, and P17.7's summary records the debt as CLOSED
  # rather than carried a sixth time. The INSTRUMENT is unchanged: those 30 rows must still pass.
  run_cfg release_cuda build/qual-final   7200 2696 2634
  run_cfg cpu_only     build/qual-cpuonly 7200 2490 2430

  echo "############################################################"
  echo "### B6: CPU-only has no CUDA dependency"
  echo "############################################################"
  # MUST fail closed: `grep -ac` on a missing file prints 0, the same answer a clean build
  # gives, so a wrong path reports "clean" without having read anything.
  CPUBUILD=$L/50_cpu_only_build.log
  if [ ! -f "$CPUBUILD" ]; then
    echo "  *** B6 INVALID: $CPUBUILD does not exist, so the counts below would read 0 for"
    echo "      the wrong reason. FAILING CLOSED on B6. ***"
  else
    echo "  build log read                    : 50_cpu_only_build.log ($(wc -l < "$CPUBUILD") lines)"
    echo "  nvcc invocations in the build log : $(grep -ac 'nvcc' "$CPUBUILD" || true)"
    echo "  libcudart references              : $(grep -ac 'libcudart' "$CPUBUILD" || true)"
  fi
  echo "  cuda/libcfdcuda.a present         : $([ -f build/qual-cpuonly/cuda/libcfdcuda.a ] && echo YES || echo no)"
  for b in build/qual-cpuonly/apps/cli/cfdapp; do
    if [ -x "$b" ]; then
      printf '  %-44s cudart refs in ldd: %s\n' "$b" "$(ldd "$b" 2>/dev/null | grep -c cudart)"
    fi
  done
  echo ""
else
  # Debug+GUI is NOT run in P17.6 (deferred to P17.11 by standing decision). Its predicted counts
  # were last maintained in P17.4 and are therefore STALE by construction -- they are left as a
  # placeholder rather than guessed at, and P17.11 must re-derive them from a real run.
  run_cfg debug_gui build/qual-debug 14400 2670 2608   # STALE: last maintained at P17.4
fi

echo "P17_CODE_FINGERPRINT after  : $(code_fp)"
echo "B8: the code fingerprint must not have moved across the regression."
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee -a "$log"

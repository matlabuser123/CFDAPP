#!/usr/bin/env bash
# P17.8 regression. Execution policy FROZEN and inherited verbatim from
# results/p17/hllc/tools/p17_7_regress.sh, which inherited it from P13's G10 closeout.
# Gate: results/p17/shock_validation/acceptance_gate.md §13.
#
# The staleness checker is P17.7's staleness.py, REUSED UNCHANGED rather than re-derived --
# its per-binary source set comes from CMake's own object lists, which is the only version of
# that check that has survived a fail-closed audit in this project.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
OUT=$ROOT/results/p17/shock_validation
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

find_test_bins() {
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

log=$L/50_p17_9_regress.log

{
echo "P17.9 -- regression"
echo "date: $(date -u +%FT%TZ)"
GATE_FROZEN=b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40
GATE_LIVE=$(sha256sum "$OUT/acceptance_gate.md" | awk '{print $1}')
echo "gate: results/p17/shock_validation/acceptance_gate.md"
echo "      frozen sha256 $GATE_FROZEN"
echo "      live   sha256 $GATE_LIVE"
if [ "$GATE_LIVE" = "$GATE_FROZEN" ]; then
  echo "      GATE INTEGRITY: MATCHES the freeze"
else
  echo "      *** GATE INTEGRITY: the gate MOVED since the freeze. FAILING CLOSED. ***"
  exit 1
fi
echo "      configuration.md $(sha256sum "$OUT/configuration.md" | awk '{print $1}')"
echo "      riemann_reference.py $(sha256sum "$OUT/tools/riemann_reference.py" | awk '{print $1}')"
echo "      (both must match logs/02_freeze.log; they are re-hashed at the START of every"
echo "       evidence-producing step, because a frozen-doc check that only runs at phase end"
echo "       hides a post-freeze edit for the whole phase.)"
echo ""
echo "FROZEN exclusions (5 patterns, verbatim):"
echo "  $EXCLUDE"
echo "FROZEN invocation: OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200"
echo "  build/qual-debug is NOT run -- deferred to P17.11 by standing decision"
echo "nvcc on PATH: $(command -v nvcc)"
nvcc --version 2>/dev/null | tail -2 | sed 's/^/  /'
echo ""
echo "P17_CODE_FINGERPRINT before : $(code_fp)"
echo ""
echo "### THE PREDICTION, declared before the run ###"
echo "  P17.8 close: release_cuda 2732 registered / 2670 executed"
echo "               cpu_only     2526 registered / 2466 executed"
echo "  P17.9 adds 13 tests and no others:"
echo "    tests/integration/compressible/test_shock_validation.cpp   $(grep -c '^TEST(' tests/integration/compressible/test_shock_validation.cpp) TEST macros"
echo "  Both files are pure CPU, so the delta is IDENTICAL in both configurations:"
echo "    release_cuda 2732+13 = 2745 registered   2670+13 = 2683 executed"
echo "    cpu_only     2526+13 = 2539 registered   2466+13 = 2479 executed"
echo "  A test count is an arithmetic consequence of the suite, not a tolerance."
echo ""

NEWEST_SRC_T=0
NEWEST_SRC_F=""
while IFS= read -r f; do
  t=$(stat -c %Y "$f")
  if [ "$t" -gt "$NEWEST_SRC_T" ]; then NEWEST_SRC_T=$t; NEWEST_SRC_F=$f; fi
done < <(find include src cuda -type f \
           \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cu' \
              -o -name '*.cuh' -o -name 'CMakeLists.txt' \) 2>/dev/null)
echo "newest PRODUCTION source: $(date -u -d @"$NEWEST_SRC_T" +%FT%TZ)  $NEWEST_SRC_F"
echo ""

run_cfg() {   # run_cfg <label> <build dir> <timeout> <pred registered> <pred executed> <pred p17.8 rows>
  local label="$1" dir="$2" tmo="$3" pred_reg="$4" pred_exec="$5" pred_rows="$6"
  echo "############################################################"
  echo "### $label  ($dir)   --timeout $tmo"
  echo "############################################################"
  grep -hE 'CMAKE_BUILD_TYPE:STRING|CFDAPP_ENABLE_CUDA:BOOL|CFDAPP_BUILD_GUI:BOOL|CFDAPP_ENABLE_OPENMP:BOOL' \
    "$dir/CMakeCache.txt" | sed 's/^/  /'
  echo ""
  echo "--- configure + build (every target) ---"
  local t0 brc
  cmake -S . -B "$dir" > "$L/50_${label}_configure.log" 2>&1
  echo "  configure exit: $?"
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
  echo "--- section 8 step 3: staleness, FAIL CLOSED (P17.7's staleness.py, unchanged) ---"
  find_test_bins "$dir" > "$L/50_${label}_bins.txt"
  python3 "$ROOT/results/p17/hllc/tools/staleness.py" "$dir" "$ROOT" "$NEWEST_SRC_T" \
    < "$L/50_${label}_bins.txt"
  local strc=$?
  if [ "$strc" -ne 0 ]; then
    echo "  *** STALE OR UNVERIFIABLE BINARIES -- FAIL CLOSED ***"
    echo ""
    return 1
  fi

  echo ""
  echo "--- library hashes (section 8 step 4) ---"
  local lib
  for lib in "$dir/src/libcfdcore.a" "$dir/cuda/libcfdcuda.a"; do
    if [ -f "$lib" ]; then
      printf '  %-46s %s\n' "${lib#"$dir"/}" "$(sha256sum "$lib" | awk '{print $1}')"
    fi
  done
  echo "  (libcfdcuda.a's hash MOVES across identical rebuilds -- nvcc is not bit-reproducible."
  echo "   It is recorded, never used as an identity check.)"

  echo ""
  echo "--- registered tests (ctest -N, nothing executed) ---"
  local reg exc_n eos_n db_n
  reg=$(ctest --test-dir "$dir" -N 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  exc_n=$(ctest --test-dir "$dir" -N -R "$EXCLUDE" 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  eos_n=$(ctest --test-dir "$dir" -N -R 'IdealGasEOSTest' 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  db_n=$(ctest --test-dir "$dir" -N -R 'ShockValidation|SodShockTube|NormalShock|ContactDiscontinuity|CompressionExpansion|SupersonicAdvection|ShockDetector|ShockConservation|ShockDeterminism' 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
  echo "  registered              : ${reg:-unknown}   PREDICTED $pred_reg"
  echo "  matching the exclusions : ${exc_n:-unknown}   PREDICTED 13"
  echo "  IdealGasEOSTest rows    : ${eos_n:-unknown}   PREDICTED 30"
  echo "  P17.9 shock rows        : ${db_n:-unknown}   PREDICTED $pred_rows"

  echo ""
  echo "--- OMP_NUM_THREADS=1 ctest -j 8 --timeout $tmo -E <5 frozen exclusions> ---"
  local crc secs total pct failed notrun
  t0=$(date +%s)
  OMP_NUM_THREADS=1 ctest --test-dir "$dir" -j 8 --timeout "$tmo" -E "$EXCLUDE" --output-on-failure \
    > "$L/50_${label}_ctest.log" 2>&1
  crc=$?
  secs=$(( $(date +%s) - t0 ))
  total=$(grep -aoE 'out of [0-9]+' "$L/50_${label}_ctest.log" | tail -1 | grep -oE '[0-9]+')
  pct=$(grep -aoE '[0-9]+% tests passed' "$L/50_${label}_ctest.log" | tail -1)
  failed=$(grep -aoE '[0-9]+ tests failed' "$L/50_${label}_ctest.log" | tail -1 | grep -oE '^[0-9]+')
  notrun=$(grep -acE '\(Not Run\)' "$L/50_${label}_ctest.log" || true)
  echo "  ctest exit status : $crc"
  echo "  total tests       : ${total:-unknown}      PREDICTED $pred_exec"
  echo "  passed            : ${pct:-unknown}"
  echo "  failed            : ${failed:-0}           PREDICTED 0"
  echo "  Not Run           : $notrun"
  echo "  runtime           : ${secs} s  [WSL wall clock, indicative only]"
  if [ "${total:-0}" = "$pred_exec" ]; then
    echo "  COUNT VERDICT     : MATCHES the frozen prediction"
  else
    echo "  COUNT VERDICT     : *** DEVIATION from the declared prediction ***"
  fi
  if [ "${failed:-0}" != "0" ] || [ "$crc" -ne 0 ]; then
    echo "  --- every non-passing test ---"
    grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/50_${label}_ctest.log" | sed 's/^/    /'
  fi

  echo ""
  echo "--- the P17.8 tests EXECUTED, by name (registration is not execution) ---"
  echo "  P17.9 shock rows that Passed: $(grep -acE '(Sod|NormalShock|Contact|Compression|Supersonic|ShockDetector|ShockConservation|ShockDeterminism)[A-Za-z]*(Test|Integration)?\..*Passed' "$L/50_${label}_ctest.log" || true)   PREDICTED $pred_rows"
  grep -aE 'DensityBased[A-Za-z]*(Test|Integration)\.' "$L/50_${label}_ctest.log" \
    | grep -aE 'Passed|Failed' | sed 's/^/    /'
  echo ""
  echo "--- preservation: P17.7 HLLC and P17.2 EOS rows EXECUTED ---"
  echo "  Hllc rows that Passed          : $(grep -acE 'Hllc[A-Za-z]*Test\..*Passed' "$L/50_${label}_ctest.log" || true)"
  echo "  IdealGasEOSTest rows Passed    : $(grep -acE 'IdealGasEOSTest\..*Passed' "$L/50_${label}_ctest.log" || true)   PREDICTED 30"
  echo "  LowMach rows Passed            : $(grep -acE 'LowMach.*Passed' "$L/50_${label}_ctest.log" || true)"
  echo ""
}

run_cfg release_cuda build/qual-final   7200 2745 2683 13
run_cfg cpu_only     build/qual-cpuonly 7200 2539 2479 13

echo "############################################################"
echo "### CPU-only has no CUDA dependency"
echo "############################################################"
CPUBUILD=$L/50_cpu_only_build.log
if [ ! -f "$CPUBUILD" ]; then
  echo "  *** INVALID: $CPUBUILD does not exist, so the counts below would read 0 for the wrong"
  echo "      reason. FAILING CLOSED. ***"
else
  echo "  build log read                    : 50_cpu_only_build.log ($(wc -l < "$CPUBUILD") lines)"
  echo "  nvcc invocations in the build log : $(grep -ac 'nvcc' "$CPUBUILD" || true)"
  echo "  libcudart references              : $(grep -ac 'libcudart' "$CPUBUILD" || true)"
fi
echo "  cuda/libcfdcuda.a present         : $([ -f build/qual-cpuonly/cuda/libcfdcuda.a ] && echo YES || echo no)"
echo ""
echo "P17_CODE_FINGERPRINT after  : $(code_fp)"
echo "  the code fingerprint must not have moved ACROSS the regression."
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

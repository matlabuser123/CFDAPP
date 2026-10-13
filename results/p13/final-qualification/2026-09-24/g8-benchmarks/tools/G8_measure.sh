#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G8, the measurement half.
#
# Frozen instruments, run unmodified:
#   scripts/benchmarks/run_suite.sh          the suite driver (S1, S2, O6)
#   scripts/benchmarks/compare_benchmarks.py the comparator (R1..R7)
#   results/p13/benchmarks/tools/verify_smoke.sh  the ctest smoke registration (S3, S4)
#
# All output goes to this gate's own directory via --output-dir. Nothing is
# written to any benchmark's default location, which is the whole point of
# criterion O2.
#
# results/p13/benchmarks/tools/final.sh is NOT run whole: beyond the smoke test it
# is the three-configuration full regression, which is G10's gate.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G8=$ROOT/results/p13/final-qualification/2026-09-24/g8-benchmarks
L=$G8/logs
D=$G8/data
BASE=$ROOT/results/p13/benchmarks/data/baseline-linux-x64
cd "$ROOT"
mkdir -p "$L" "$D"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

log=$L/G8_measure.log
{
echo "P13 G8 -- production benchmarks, measurement half"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### Step 10 (before) -- inventory of the TRACKED evidence that must not move ###"
git ls-files results/performance benchmarks > /tmp/g8_immutable_list.txt
echo "  tracked files in results/performance + benchmarks: $(wc -l < /tmp/g8_immutable_list.txt)"
sha256sum $(cat /tmp/g8_immutable_list.txt) > "$D/immutable_before.sha256"
echo "  recorded: $D/immutable_before.sha256"
echo "  NOTE: parts of results/ ARE tracked -- benchmarks/results.csv and 45 others."
echo "  The 5 file-writing benchmarks default to results/performance/{baseline,"
echo "  openmp_scaling,cuda_end_to_end,preconditioner,large_grid_stress}, which is"
echo "  exactly what O2 protects."
echo ""

echo "### Step 7 -- the frozen threshold, unchanged ###"
echo "  frozen value (acceptance_gate.md + amendment A1): 1.25x"
grep -nE 'default=1\.25|"--threshold"' -A2 scripts/benchmarks/compare_benchmarks.py | head -8 | sed 's/^/    /'
echo "  A1 did NOT raise it: the load guard controls the variable instead."
echo ""

echo "### Step 6 (a) -- machine state, and the guard's own arithmetic ###"
CORES=$(nproc)
LOAD1=$(cut -d' ' -f1 /proc/loadavg)
LIMIT=$(awk -v c="$CORES" 'BEGIN{printf "%.2f", c*0.25}')
echo "  cores=$CORES  loadavg(1m)=$LOAD1  guard limit (25% of cores)=$LIMIT"
awk -v l="$LOAD1" -v m="$LIMIT" 'BEGIN{ if (l>m) print "  machine is LOADED -- the driver should refuse"; else print "  machine is QUIET -- the driver should proceed" }'
echo ""

echo "### Step 8 -- the ctest benchmark smoke entry, BY NAME ###"
bash results/p13/benchmarks/tools/verify_smoke.sh 2>&1 | sed 's/^/  /'
echo ""

echo "### Step 2/3 -- the suite driver, into this gate's directory ###"
RUN=$D/suite_current
rm -rf "$RUN"
bash scripts/benchmarks/run_suite.sh --output-dir "$RUN" --build build/final > "$L/G8_suite.log" 2>&1
SUITE_RC=$?
echo "  driver exit status: $SUITE_RC   (0 required)"
echo "  per-benchmark result, as the driver reported it:"
grep -aE '^(RUN|PASS|FAIL|ok|==|benchmark)' "$L/G8_suite.log" | head -20 | sed 's/^/    /'
tail -12 "$L/G8_suite.log" | sed 's/^/    /'
echo "  files the driver produced:"
ls -1 "$RUN" 2>/dev/null | sed 's/^/    /'
echo "  environment captured alongside (criterion S2, incl. A1's load keys):"
sed 's/^/    /' "$RUN/suite_environment.txt" 2>/dev/null || echo "    MISSING"
echo ""

echo "### Step 4 -- the comparator on genuine output: current vs the committed baseline ###"
python3 scripts/benchmarks/compare_benchmarks.py --baseline "$BASE" --current "$RUN" \
  > "$L/G8_compare_normal.log" 2>&1
CMP_RC=$?
echo "  comparator exit status: $CMP_RC   (0 required)"
sed 's/^/    /' "$L/G8_compare_normal.log"
echo ""

echo "### Step 10 (after) -- the tracked evidence must be byte-identical ###"
sha256sum -c "$D/immutable_before.sha256" --quiet > "$L/G8_immutable_after.log" 2>&1
IMM_RC=$?
CHANGED=$(grep -c ':' "$L/G8_immutable_after.log" 2>/dev/null || echo 0)
echo "  tracked evidence changed files = $CHANGED   (0 required)"
[ "$IMM_RC" -eq 0 ] && echo "  all $(wc -l < /tmp/g8_immutable_list.txt) tracked files byte-identical" \
                    || sed 's/^/    /' "$L/G8_immutable_after.log"
echo ""

echo "### Step 11 -- filesystem hygiene ###"
echo "  benchmark CSV/JSON in the repository root:"
find . -maxdepth 1 \( -name '*.csv' -o -name 'benchmark_*.json' \) -printf '    STRAY: %p\n' 2>/dev/null || true
[ -z "$(find . -maxdepth 1 \( -name '*.csv' -o -name 'benchmark_*.json' \) 2>/dev/null)" ] && echo "    none"
echo "  did anything land in the benchmarks' DEFAULT dirs during this run?"
for d in baseline openmp_scaling cuda_end_to_end preconditioner large_grid_stress; do
  newest=$(find "results/performance/$d" -type f -newermt '-20 minutes' 2>/dev/null | wc -l)
  printf '    results/performance/%-20s files modified in the last 20 min: %s\n' "$d" "$newest"
done
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

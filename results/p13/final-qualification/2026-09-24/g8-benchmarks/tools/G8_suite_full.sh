#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G8, the suite driver against the COMPLETE suite.
#
# WHY A SECOND RUN, recorded rather than quietly replacing the first:
# The first invocation (logs/G8_measure.log) pointed the driver at build/final,
# which has CFDAPP_ENABLE_CUDA:BOOL=OFF, so the two GPU benchmarks were never
# built there. The driver reported them MISSING and exited 1. That is the driver
# behaving exactly as designed -- its own comment says "Not silently skipped: a
# suite that quietly runs fewer benchmarks than it claims still reports green" --
# and the first run is therefore PRESERVED as positive evidence for criterion S1
# rather than deleted as a mistake. The mistake was mine: the wrong --build.
#
# Two further corrections to my own wrapper:
#  1. No rebuild runs before the measurement. The first attempt had
#     verify_smoke.sh rebuild three configurations immediately beforehand, which
#     drove the 1-minute load to 8.38 against a guard limit of 8.00 -- exactly
#     the contamination amendment A1 exists to prevent.
#  2. The machine is required to be quiet BEFORE the driver is invoked, and the
#     load is recorded here as well as by the driver.
#
# --allow-loaded is deliberately NOT used. It would stamp the recording
# "NOT COMPARABLE" and make the comparator verdict worthless.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G8=$ROOT/results/p13/final-qualification/2026-09-24/g8-benchmarks
L=$G8/logs
D=$G8/data
BASE=$ROOT/results/p13/benchmarks/data/baseline-linux-x64
BUILD=build/qual-final     # the G1 qualification tree: Release + CUDA ON
cd "$ROOT"
mkdir -p "$L" "$D"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

log=$L/G8_suite_full.log
{
echo "P13 G8 -- the complete suite, against $BUILD"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### the build tree, and why this one ###"
grep -E 'CFDAPP_ENABLE_CUDA|CMAKE_BUILD_TYPE|CFDAPP_BUILD_BENCHMARKS' "$BUILD/CMakeCache.txt" | sed 's/^/  /'
for b in benchmarks/cpu/cfd_benchmark_cpu \
         benchmarks/gpu/cfd_benchmark_cuda_end_to_end \
         benchmarks/gpu/cfd_benchmark_gpu_preconditioner; do
  printf '  %-52s %s\n' "$b" "$([ -x "$BUILD/$b" ] && echo present || echo MISSING)"
done
echo "  (build/final has CUDA OFF, which is why the first run failed -- correctly)"
echo ""

echo "### wait for the machine to settle below the guard limit ###"
CORES=$(nproc); LIMIT=$(awk -v c="$CORES" 'BEGIN{printf "%.2f", c*0.25}')
for i in $(seq 1 40); do
  L1=$(cut -d' ' -f1 /proc/loadavg)
  if awk -v l="$L1" -v m="$LIMIT" 'BEGIN{exit !(l<=m*0.5)}'; then break; fi
  sleep 15
done
echo "  cores=$CORES  guard limit=$LIMIT  loadavg(1m) now=$(cut -d' ' -f1 /proc/loadavg)"
echo "  waited $(( (i-1) * 15 )) s for the machine to settle"
echo ""

echo "### Step 10 (before) -- tracked evidence inventory ###"
git ls-files results/performance benchmarks > /tmp/g8_immutable_list.txt
sha256sum $(cat /tmp/g8_immutable_list.txt) > "$D/immutable_before_full.sha256"
echo "  $(wc -l < /tmp/g8_immutable_list.txt) tracked files hashed"
echo ""

echo "### Step 2 -- the driver, COMPLETE suite ###"
RUN=$D/suite_full
rm -rf "$RUN"
bash scripts/benchmarks/run_suite.sh --output-dir "$RUN" --build "$BUILD" > "$L/G8_suite_full_raw.log" 2>&1
RC=$?
echo "  driver exit status: $RC   (0 required)"
echo ""
echo "  per-benchmark result:"
grep -aE ': (ok|FAILED|MISSING)' "$L/G8_suite_full_raw.log" | sed 's/^/  /'
echo ""
echo "  the driver's own tail:"
tail -18 "$L/G8_suite_full_raw.log" | sed 's/^/    /'
echo ""
echo "  files produced:"
ls -1 "$RUN" 2>/dev/null | sed 's/^/    /'
echo ""
echo "  environment captured (S2, incl. A1's load keys):"
sed 's/^/    /' "$RUN/suite_environment.txt" 2>/dev/null || echo "    MISSING"
echo ""

echo "### Step 4 -- comparator: this run vs the committed baseline ###"
python3 scripts/benchmarks/compare_benchmarks.py --baseline "$BASE" --current "$RUN" \
  > "$L/G8_compare_full.log" 2>&1
CRC=$?
echo "  comparator exit status: $CRC"
sed 's/^/    /' "$L/G8_compare_full.log"
echo ""

echo "### Step 10 (after) ###"
if sha256sum -c "$D/immutable_before_full.sha256" --quiet > "$L/G8_immutable_after_full.log" 2>&1; then
  echo "  tracked evidence changed files = 0   -- all $(wc -l < /tmp/g8_immutable_list.txt) byte-identical"
else
  echo "  *** TRACKED EVIDENCE CHANGED ***"; sed 's/^/    /' "$L/G8_immutable_after_full.log"
fi
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

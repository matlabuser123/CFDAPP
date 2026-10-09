#!/usr/bin/env bash
# P13-007 -- re-run of NV2/NV4/NV5/NV6 after the cause NV6 exposed.
#
# The baseline had been recorded BEFORE amendment A1 added loadavg/loaded to
# suite_environment.txt, so it carried no `loaded` key. The environment check
# then refused every comparison against it, and NV2/NV4 reported DETECTED
# without the comparator ever reaching the regression logic. NV6 -- which must
# STOP failing when the threshold is raised -- is what exposed that.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $ROOT
CMP="python3 $ROOT/scripts/benchmarks/compare_benchmarks.py"
BASE=$ROOT/results/p13/benchmarks/data/baseline-linux-x64
verdict() { [ "$2" = "yes" ] && echo "  $1: DETECTED -- $3" || echo "  $1: *** UNDETECTED *** -- $3"; }

echo "loadavg now: $(cut -d' ' -f1 /proc/loadavg)"
echo
echo "=== re-record the baseline with the current driver ==="
rm -rf $BASE
bash scripts/benchmarks/run_suite.sh --quick --output-dir $BASE > /tmp/rerec.log 2>&1
echo "  exit: $?"
grep -E "loaded|loadavg|cpu |cfdapp" $BASE/suite_environment.txt | sed 's/^/    /'

RUN=/tmp/p13_bm_run2; rm -rf $RUN
bash scripts/benchmarks/run_suite.sh --quick --output-dir $RUN > /tmp/rerun.log 2>&1
echo "  reference run exit: $?"

echo
echo "=== R3 sanity: the two must now actually COMPARE ==="
$CMP --baseline $BASE --current $RUN > /tmp/r3.log 2>&1; rc=$?
tail -6 /tmp/r3.log | sed 's/^/    /'
echo "  exit: $rc"
if ! grep -q "metrics compared" /tmp/r3.log; then
  echo "  STOP: still not comparing -- the controls below would prove nothing again."
  exit 1
fi

echo
echo "########## NV2 -- inject a 1.60x slowdown; R4 must fail ON THE THRESHOLD"
M=/tmp/p13_bm_slow2; rm -rf $M; cp -r $RUN $M
python3 - <<'PY'
import json
f='/tmp/p13_bm_slow2/benchmark_80x80.json'
d=json.load(open(f)); d['runtime_per_iteration_ms']*=1.60
json.dump(d,open(f,'w'),indent=2)
PY
$CMP --baseline $BASE --current $M > /tmp/nv2b.log 2>&1; rc=$?
grep -E "REGRESSION|FAIL|metrics compared" /tmp/nv2b.log | head -4 | sed 's/^/    /'
if [ $rc -ne 0 ] && grep -q "REGRESSION" /tmp/nv2b.log; then
  verdict NV2 yes "exits $rc naming the regressed metric"
else
  verdict NV2 no "exit $rc, no REGRESSION line"
fi

echo
echo "########## NV6 -- same data at --threshold 2.0; it must now PASS"
$CMP --baseline $BASE --current $M --threshold 2.0 > /tmp/nv6b.log 2>&1; rc=$?
tail -2 /tmp/nv6b.log | sed 's/^/    /'
[ $rc -eq 0 ] && verdict NV6 yes "exits 0 at 2.0x -- the THRESHOLD detects, not the plumbing" \
              || verdict NV6 no "still fails at 2.0x"

echo
echo "########## NV4 -- delete a case; R6 must fail as a MISSING CASE"
D=/tmp/p13_bm_missing2; rm -rf $D; cp -r $RUN $D; rm -f $D/benchmark_80x80.json
$CMP --baseline $BASE --current $D > /tmp/nv4b.log 2>&1; rc=$?
tail -2 /tmp/nv4b.log | sed 's/^/    /'
if [ $rc -ne 0 ] && grep -q "missing" /tmp/nv4b.log; then
  verdict NV4 yes "reported the missing case"
else
  verdict NV4 no "exit $rc, not a missing-case failure"
fi

echo
echo "########## NV5 -- mismatched CPU; E2 must refuse NAMING the cpu"
X=/tmp/p13_bm_env2; rm -rf $X; cp -r $RUN $X
sed -i 's/^cpu     :.*/cpu     : Some Other CPU/' $X/suite_environment.txt
$CMP --baseline $BASE --current $X > /tmp/nv5b.log 2>&1; rc=$?
grep -E "^  cpu|FAIL" /tmp/nv5b.log | head -3 | sed 's/^/    /'
if [ $rc -ne 0 ] && grep -q "^  cpu:" /tmp/nv5b.log; then
  verdict NV5 yes "refused, naming the cpu mismatch"
else
  verdict NV5 no "exit $rc, cpu mismatch not named"
fi

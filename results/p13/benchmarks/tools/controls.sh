#!/usr/bin/env bash
# P13-007 non-vacuity controls. Each breaks one thing and requires the named
# criterion to FAIL. Sources restored from a pre-control copy, sha256-verified.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $ROOT
export PATH=/usr/local/cuda-12.9/bin:$PATH
CMP="python3 $ROOT/scripts/benchmarks/compare_benchmarks.py"
BASE=$ROOT/results/p13/benchmarks/data/baseline-linux-x64
BK=/tmp/p13_bm_controls; rm -rf $BK; mkdir -p $BK
FILES="benchmarks/BenchmarkOutput.hpp scripts/benchmarks/run_suite.sh"
for f in $FILES; do cp "$f" "$BK/$(echo $f | tr / _)"; done
sha256sum $FILES > $BK/pre.sha256
restore() {
  for f in $FILES; do cp "$BK/$(echo $f | tr / _)" "$f"; done
  sha256sum -c $BK/pre.sha256 --quiet && echo "  RESTORED (sha256 verified)" || { echo "  *** RESTORE FAILED ***"; exit 9; }
}
verdict() { [ "$2" = "yes" ] && echo "  $1: DETECTED -- $3" || echo "  $1: *** UNDETECTED *** -- $3"; }

# A clean current run to mutate for the comparator controls.
RUN=/tmp/p13_bm_run; rm -rf $RUN
bash scripts/benchmarks/run_suite.sh --quick --output-dir $RUN > /tmp/p13_bm_run.log 2>&1
echo "reference run recorded (exit $?)"

echo
echo "########## NV2 -- inject a slowdown; R4 must fail"
M=/tmp/p13_bm_slow; rm -rf $M; cp -r $RUN $M
python3 - <<'PY'
import json,glob
for f in glob.glob('/tmp/p13_bm_slow/benchmark_80x80.json'):
    d=json.load(open(f)); d['runtime_per_iteration_ms']*=1.60
    json.dump(d,open(f,'w'),indent=2)
PY
$CMP --baseline $BASE --current $M > /tmp/nv2.log 2>&1; rc=$?
tail -4 /tmp/nv2.log | sed 's/^/    /'
[ $rc -ne 0 ] && verdict NV2 yes "comparator exits $rc" || verdict NV2 no "still exits 0"

echo
echo "########## NV6 -- raise the threshold above the injection; R4 must stop firing"
$CMP --baseline $BASE --current $M --threshold 2.0 > /tmp/nv6.log 2>&1; rc=$?
tail -2 /tmp/nv6.log | sed 's/^/    /'
[ $rc -eq 0 ] && verdict NV6 yes "exits 0 at 2.0x -- the THRESHOLD is what detects, not the plumbing" \
              || verdict NV6 no "still fails at 2.0x"

echo
echo "########## NV4 -- delete a case from the current run; R6 must fail"
D=/tmp/p13_bm_missing; rm -rf $D; cp -r $RUN $D; rm -f $D/benchmark_80x80.json
$CMP --baseline $BASE --current $D > /tmp/nv4.log 2>&1; rc=$?
tail -2 /tmp/nv4.log | sed 's/^/    /'
[ $rc -ne 0 ] && verdict NV4 yes "missing case is a failure, not a skip" || verdict NV4 no "passed anyway"

echo
echo "########## NV3 -- a baseline with no comparable metric; R5 must fail"
E=/tmp/p13_bm_empty; rm -rf $E; mkdir -p $E; cp $RUN/suite_environment.txt $E/
python3 - <<'PY'
import json
for n in ("20x20","40x40","80x80"):
    json.dump({"note":"no aggregate metric here"},open(f"/tmp/p13_bm_empty/benchmark_{n}.json","w"))
PY
$CMP --baseline $E --current $RUN > /tmp/nv3.log 2>&1; rc=$?
tail -2 /tmp/nv3.log | sed 's/^/    /'
[ $rc -ne 0 ] && verdict NV3 yes "comparing nothing is a failure" || verdict NV3 no "passed with 0 comparisons"

echo
echo "########## NV5 -- mismatched BUILD CONFIGURATION; E2 must refuse"
# G8-F2: this control used to mutate `cpu`, a MACHINE key, while its title claimed
# to test a build-configuration mismatch. It therefore never exercised the half of
# E2 it named. It now mutates openmp_enabled in environment.json -- the file the
# comparator had never opened (G8-F1) -- and requires the refusal to NAME the key,
# not merely to exit non-zero.
X=/tmp/p13_bm_env; rm -rf $X; cp -r $RUN $X
python3 - "$X/environment.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
before = d.get("openmp_enabled")
d["openmp_enabled"] = not bool(before)
json.dump(d, open(p, "w"), indent=2)
print("  planted: openmp_enabled %r -> %r" % (before, d["openmp_enabled"]))
PY
$CMP --baseline $BASE --current $X > /tmp/nv5.log 2>&1; rc=$?
tail -4 /tmp/nv5.log | sed 's/^/    /'
if [ $rc -ne 0 ] && grep -q 'openmp_enabled' /tmp/nv5.log; then
  verdict NV5 yes "refused, NAMING the openmp_enabled mismatch"
elif [ $rc -ne 0 ]; then
  verdict NV5 no "refused, but did NOT name openmp_enabled -- wrong reason"
else
  verdict NV5 no "compared anyway -- E2 does not see build configuration"
fi

echo
echo "########## NV5b -- mismatched MACHINE; E2 must refuse"
# The original NV5 case, kept as its own control. It is a real check of the guard;
# it simply is not the build-configuration check NV5's title promised.
Y=/tmp/p13_bm_env_cpu; rm -rf $Y; cp -r $RUN $Y
sed -i 's/^cpu     :.*/cpu     : Some Other CPU/' $Y/suite_environment.txt
$CMP --baseline $BASE --current $Y > /tmp/nv5b.log 2>&1; rc=$?
tail -3 /tmp/nv5b.log | sed 's/^/    /'
if [ $rc -ne 0 ] && grep -q 'cpu:' /tmp/nv5b.log; then
  verdict NV5b yes "refused, NAMING the cpu mismatch"
elif [ $rc -ne 0 ]; then
  verdict NV5b no "refused, but did NOT name cpu -- wrong reason"
else
  verdict NV5b no "compared anyway"
fi

echo
echo "########## NV7 -- remove the load guard; O6 must stop protecting"
python3 - <<'PY'
import io
p='scripts/benchmarks/run_suite.sh'; s=io.open(p,encoding='utf-8',newline='').read()
o='if [ "$ALLOW_LOADED" -eq 0 ] &&    awk -v l="$LOAD1" -v m="$LOAD_LIMIT" \'BEGIN{exit !(l>m)}\'; then'
assert s.count(o)==1, "load guard anchor missing"
s=s.replace(o,'if false; then',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
# Manufacture load, then try to record.
for i in $(seq 1 40); do (while :; do :; done) & done
LOADPIDS=$(jobs -p)
sleep 62
echo "  loadavg under artificial load: $(cut -d' ' -f1 /proc/loadavg)"
G=/tmp/p13_bm_loaded; rm -rf $G
bash scripts/benchmarks/run_suite.sh --quick --output-dir $G > /tmp/nv7_noguard.log 2>&1
NOGUARD=$?
echo "  without the guard -> exit $NOGUARD"
restore
bash scripts/benchmarks/run_suite.sh --quick --output-dir /tmp/p13_bm_guarded > /tmp/nv7_guard.log 2>&1
WITHGUARD=$?
echo "  with the guard    -> exit $WITHGUARD"
head -2 /tmp/nv7_guard.log | sed 's/^/    /'
kill $LOADPIDS 2>/dev/null
[ $NOGUARD -eq 0 ] && [ $WITHGUARD -ne 0 ] && verdict NV7 yes "the guard is what refuses a loaded run" \
  || verdict NV7 no "noguard=$NOGUARD guard=$WITHGUARD"

echo
echo "########## NV1 -- ignore --output-dir; O1/O2 must break"
python3 - <<'PY'
import io
p='benchmarks/BenchmarkOutput.hpp'; s=io.open(p,encoding='utf-8',newline='').read()
o='    out = std::filesystem::path(argv[++i]);'
assert s.count(o)==1
s=s.replace(o,'    ++i;  // NV1: deliberately ignore the requested directory',1)
io.open(p,'w',encoding='utf-8',newline='').write(s)
PY
cmake --build build/final -j 12 >/dev/null 2>&1
N=/tmp/p13_bm_nv1; rm -rf $N; mkdir -p $N
cd /tmp
$ROOT/build/final/benchmarks/cpu/cfd_benchmark_cpu --output-dir $N >/dev/null 2>&1
cd $ROOT
COUNT=$(ls $N 2>/dev/null | wc -l)
echo "  files written to the requested directory: $COUNT"
[ "$COUNT" -eq 0 ] && verdict NV1 yes "output ignored the requested directory" || verdict NV1 no "$COUNT files still arrived"
restore
cmake --build build/final -j 12 >/dev/null 2>&1
echo
sha256sum -c $BK/pre.sha256 && git status --porcelain -- $FILES

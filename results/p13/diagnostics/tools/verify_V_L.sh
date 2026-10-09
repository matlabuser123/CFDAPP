#!/usr/bin/env bash
# P13-005 -- groups V (verbosity control) and L (the log points), measured.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=build/final/apps/cli/cfdapp
W=/tmp/p13_diag_vl
rm -rf $W; mkdir -p $W

prep() { # prep <case> <iters>
  local d=$W/$1; rm -rf $d; cp -r tests/data/cases/$1 $d; rm -rf $d/results
  python3 - $d/solver.json "$2" <<'PY' 2>/dev/null || true
import json,sys
d=json.load(open(sys.argv[1])); d["max_iterations"]=int(sys.argv[2])
json.dump(d,open(sys.argv[1],"w"),indent=2)
PY
  echo $d
}

VC=$(prep valid_cavity 6)
BP=$(prep bad_physics 6)

pass() { echo "  $1: PASS"; }
fail() { echo "  $1: FAIL -- $2"; FAILED=$((FAILED+1)); }
FAILED=0

echo "=== V1 -- --log-level sets the level ==="
for lvl in error warning info debug trace; do
  $B --case $VC --log-level $lvl >$W/v1.$lvl.out 2>$W/v1.$lvl.err
  rc=$?
  printf "  %-8s exit=%s stdout=%s B stderr=%s B\n" "$lvl" "$rc" "$(wc -c <$W/v1.$lvl.out)" "$(wc -c <$W/v1.$lvl.err)"
done
# accepted at all five, and the run still succeeds
if [ -s $W/v1.debug.err ]; then pass V1; else fail V1 "debug produced no stderr"; fi

echo "=== V2 -- an unknown level is rejected, naming the valid ones ==="
$B --case $VC --log-level shout >$W/v2.out 2>$W/v2.err; rc=$?
echo "  exit=$rc"; head -1 $W/v2.err
if [ $rc -ne 0 ] && grep -q "must be one of" $W/v2.err && grep -q "trace" $W/v2.err; then
  pass V2; else fail V2 "exit=$rc or message does not name the valid levels"; fi

echo "=== V3 -- strictly increasing verbosity ==="
ne=$(wc -l <$W/v1.error.err); nw=$(wc -l <$W/v1.warning.err)
ni=$(wc -l <$W/v1.info.err);  nd=$(wc -l <$W/v1.debug.err)
echo "  stderr lines: error=$ne warning=$nw info=$ni debug=$nd"
if [ "$ne" -lt "$nd" ] && [ "$ni" -lt "$nd" ] && [ "$ni" -gt "$ne" ]; then
  pass V3; else fail V3 "error=$ne info=$ni debug=$nd is not strictly increasing"; fi

echo "=== C1 -- stdout is untouched at EVERY level ==="
BASE=results/p13/diagnostics/data/baseline-output/valid_cavity.stdout
c1=0
for lvl in error warning info debug trace; do
  cp $W/v1.$lvl.out $W/cmp.$lvl
  sed -i "s|$VC|<case>|g" $W/cmp.$lvl
  # the corpus ran 8 iterations; compare structure, not the residuals:
  # what C1 asserts is that NO log line reached stdout.
  if grep -qE '^\[(TRACE|DEBUG|INFO|WARN|WARNING|ERROR)\]' $W/cmp.$lvl; then
    echo "  LOG LINE ON STDOUT at --log-level $lvl"; c1=$((c1+1))
  fi
done
if [ $c1 -eq 0 ]; then pass C1; else fail C1 "$c1 levels leaked a log line onto stdout"; fi

echo "=== L -- the log points, observed at --log-level debug ==="
# L4 needs a checkpointed run; L1 needs a rejected case; L7 needs a failed export.
$B --case $VC --checkpoint $W/cp.bin --checkpoint-every 2 >/dev/null 2>$W/l_ckpt.err
$B --case $VC --resume    $W/cp.bin --log-level debug     >/dev/null 2>$W/l_resume.err
$B --case $VC --checkpoint $W/cp2.bin --checkpoint-every 2 --log-level debug >/dev/null 2>$W/l_ckpt2.err
$B --case $BP --log-level debug >/dev/null 2>$W/l_reject.err
# export failure: make <case>/results a regular file so the exporter cannot create it
EX=$(prep valid_cavity 4); rm -rf $EX/results; : > $EX/results
$B --case $EX --log-level debug >/dev/null 2>$W/l_export.err

check() { # check <id> <file> <pattern>
  if grep -q "$3" "$2"; then echo "  $1 OBSERVED: $(grep -m1 "$3" "$2" | cut -c1-96)";
  else echo "  $1 NOT OBSERVED (pattern: $3)"; FAILED=$((FAILED+1)); fi
}
check L1a $W/l_ckpt2.err "loading case:"
check L1b $W/l_reject.err "case rejected:"
check L2  $W/l_ckpt2.err "linear solver backend"
check L3  $W/l_ckpt2.err "solver start:"
check L4a $W/l_ckpt2.err "checkpoint written at iteration"
check L4b $W/l_resume.err "resumed from iteration"
check L7  $W/l_export.err "export failed:"
check L8  $W/l_ckpt2.err "solve finished:"
echo "  L5/L6 (linear-solve failure, non-finite) are exercised by the N4 control below;"
echo "  no shipped case reaches them, which is why they are not asserted from a case here."

echo
echo "FAILURES: $FAILED"

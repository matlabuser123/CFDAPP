#!/usr/bin/env bash
# G8-F1 / G8-F2 verification.
#
# Three things must all hold. The third is what stops the fix being a guard that
# refuses everything -- which would satisfy the refusal test while breaking every
# real comparison (the G5 N3 lesson: an instrument that fires everywhere proves
# nothing).
#
#   1. The comparison that produced the three FALSE regressions must now REFUSE,
#      naming openmp_enabled.
#   2. A run whose build configuration MATCHES the baseline must still compare and
#      pass -- the fix must not refuse legitimate work.
#   3. Absence must fail closed: a run with no environment.json must be refused,
#      not treated as comparable to everything.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G8=$ROOT/results/p13/final-qualification/2026-09-24/g8-benchmarks
L=$G8/logs
CMP="python3 $ROOT/scripts/benchmarks/compare_benchmarks.py"
BASE=$ROOT/results/p13/benchmarks/data/baseline-linux-x64
cd "$ROOT"

log=$L/G8_verify_fix.log
{
echo "G8-F1 / G8-F2 verification"
echo "date: $(date -u +%FT%TZ)"
echo ""
echo "### the build configuration of each recorded run ###"
for d in "$BASE" "$G8/data/suite_full" "$G8/data/suite_current"; do
  [ -f "$d/environment.json" ] || { printf '  %-28s (no environment.json)\n' "$(basename "$d")"; continue; }
  printf '  %-28s openmp=%-6s cuda=%-6s build_type=%s\n' "$(basename "$d")" \
    "$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('openmp_enabled'))" "$d/environment.json")" \
    "$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('cuda_enabled_at_build'))" "$d/environment.json")" \
    "$(python3 -c "import json,sys;print(json.load(open(sys.argv[1])).get('build_type'))" "$d/environment.json")"
done
echo ""

echo "############ 1. the reproducer: OpenMP-ON run vs OpenMP-OFF baseline ############"
echo "  before the fix this reported 2.67x / 2.33x / 1.60x REGRESSION"
$CMP --baseline "$BASE" --current "$G8/data/suite_full" > /tmp/g8v1.log 2>&1
rc1=$?
sed 's/^/    /' /tmp/g8v1.log | head -12
echo "  exit status: $rc1"
if [ $rc1 -ne 0 ] && grep -q 'openmp_enabled' /tmp/g8v1.log && grep -q 'refusing to compare' /tmp/g8v1.log; then
  echo "  RESULT: REFUSED, naming openmp_enabled -- G8-F1 FIXED"
  r1=ok
else
  echo "  RESULT: *** NOT FIXED *** (exit $rc1; openmp named: $(grep -c openmp_enabled /tmp/g8v1.log))"
  r1=bad
fi
echo ""

echo "############ 2. non-vacuity: a MATCHING configuration must still compare ############"
echo "  suite_current was recorded from build/final -- openmp_enabled false, as the baseline is"
$CMP --baseline "$BASE" --current "$G8/data/suite_current" > /tmp/g8v2.log 2>&1
rc2=$?
sed 's/^/    /' /tmp/g8v2.log | head -12
echo "  exit status: $rc2"
if [ $rc2 -eq 0 ] && grep -qE 'PASS: [0-9]+ metrics compared' /tmp/g8v2.log; then
  echo "  RESULT: compared and PASSED -- the guard does not refuse legitimate work"
  r2=ok
else
  echo "  RESULT: *** the fix refuses a legitimate comparison -- it fires everywhere ***"
  r2=bad
fi
echo ""

echo "############ 3. absence fails closed ############"
Z=/tmp/g8_no_env; rm -rf $Z; cp -r "$G8/data/suite_current" $Z; rm -f $Z/environment.json
$CMP --baseline "$BASE" --current $Z > /tmp/g8v3.log 2>&1
rc3=$?
sed 's/^/    /' /tmp/g8v3.log | head -8
echo "  exit status: $rc3"
if [ $rc3 -ne 0 ] && grep -q 'environment.json' /tmp/g8v3.log; then
  echo "  RESULT: REFUSED -- a run with no build configuration is not comparable to everything"
  r3=ok
else
  echo "  RESULT: *** absence passed silently ***"
  r3=bad
fi
rm -rf $Z
echo ""

echo "############ verdict ############"
printf '  1 reproducer refused naming openmp_enabled : %s\n' "$r1"
printf '  2 matching configuration still compares    : %s\n' "$r2"
printf '  3 absence fails closed                     : %s\n' "$r3"
[ "$r1" = ok ] && [ "$r2" = ok ] && [ "$r3" = ok ] \
  && echo "  G8-F1: FIXED and non-vacuous" \
  || echo "  G8-F1: NOT ESTABLISHED"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

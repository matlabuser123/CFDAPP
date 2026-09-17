#!/usr/bin/env bash
# P12-GRAD-002 A3 fresh execution: C10-A3(d) and (e) on freshly regenerated dumps, then the
# carried-over A2 step 4 (cur/nograd suites, C8 / C11(b) comparisons) under "a3_" log names.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
A2=$R/results/p12-grad-002/a2
P=$R/results/p12-grad-002/a3
[ -e $P/logs/00_freeze.log ] || { echo "REFUSED: A3 is not frozen"; exit 1; }
STEP=${1:-all}
cd $R
if [ "$STEP" = all ] || [ "$STEP" = c10 ]; then
  W=$HOME/g2/c10_a3; mkdir -p $W
  for tree in repo nograd; do bash $A2/tools/run_a2.sh a2_c10 tmpa3_c10_$tree.log $tree > /dev/null & done
  wait
  for tree in repo nograd; do
    { head -2 $A2/logs/tmpa3_c10_$tree.log; echo "dump sha256 $(sha256sum $A2/logs/tmpa3_c10_$tree.log | cut -d' ' -f1)"; } > $P/logs/fresh_c10_dump_$tree.header.log
    mv $A2/logs/tmpa3_c10_$tree.log $W/dump_$tree.txt
  done
  python3 $P/tools/selftest_c10.py $W/dump_repo.txt $W/dump_nograd.txt > $P/logs/fresh_selftest.log 2>&1; echo "exit $?" >> $P/logs/fresh_selftest.log
  python3 $A2/tools/compare_c10.py $W/dump_repo.txt $W/dump_nograd.txt > $P/logs/fresh_c10_repo_vs_nograd.log 2>&1; echo "exit $?" >> $P/logs/fresh_c10_repo_vs_nograd.log
  bash $P/tools/mutant_c10.sh fresh $W/dump_repo.txt > /dev/null
  tail -2 $P/logs/fresh_selftest.log
fi
if [ "$STEP" = all ] || [ "$STEP" = suites ]; then
  bash $A2/tools/run_suite.sh cur 6 a3_suite > /dev/null &
  bash $A2/tools/run_suite.sh nograd 6 a3_suite > /dev/null &
  wait
  for t in cur nograd; do cp $A2/logs/a3_suite_$t.log $P/logs/; done
  if grep -q "stale executables: 0" $A2/logs/a3_suite_cur.log && grep -q "stale executables: 0" $A2/logs/a3_suite_nograd.log \
     && grep -q "^ctest exit" $A2/logs/a3_suite_cur.log && grep -q "^ctest exit" $A2/logs/a3_suite_nograd.log; then
    bash $A2/tools/dry_c8_c11.sh a3 > /dev/null
    for f in a3_c8_numbers.log a3_c8_categories.log a3_c11b_outputs.log a3_c11b_fields.log; do cp $A2/logs/$f $P/logs/; done
    echo "suites and comparisons done"
  else
    echo "FAIL CLOSED: a suite did not run ctest; comparisons NOT run"
  fi
fi

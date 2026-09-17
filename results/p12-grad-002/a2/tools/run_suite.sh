#!/usr/bin/env bash
# P12-GRAD-002 A2 (C8, C11(b)): the full ctest suite in an isolated tree (cur or nograd), verbose,
# starting from the SAME generated-output state (the W10 pre-run snapshot of every tracked
# generated file), so that every generated file and every printed number can be compared between
# the two trees. usage: run_suite.sh <cur|nograd> <jobs> [log prefix, default suite]
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
TREE=$1; J=${2:-6}; PREFIX=${3:-suite}
T=$HOME/g2/$TREE
LOG=$P/logs/${PREFIX}_$TREE.log
{
  echo "# A2 suite $TREE $(date -u +%Y-%m-%dT%H:%M:%SZ); libcfdcore.a $(sha256sum $T/build/src/libcfdcore.a | cut -d' ' -f1)"
  NT=$(find $T/src_tree/src $T/src_tree/include $T/src_tree/tests -type f -printf '%T@\n' | sort -rn | head -1)
  STALE=0
  while read -r t bin; do awk -v a="$t" -v b="$NT" 'BEGIN{exit !(a<b)}' && { STALE=$((STALE+1)); echo "STALE $bin"; }; done \
    < <(find $T/build -type f \( -name 'CFD*Tests' -o -name 'cfdapp*' \) -perm -u+x -printf '%T@ %p\n')
  echo "# stale executables: $STALE"
  [ "$STALE" = 0 ] || { echo "FAIL CLOSED: stale"; exit 1; }
  [ -d $HOME/w10/snapshot ] || { echo "FAIL CLOSED: no W10 snapshot"; exit 1; }
  rsync -a $HOME/w10/snapshot/ $T/src_tree/
  echo "# generated-output state restored from the W10 snapshot ($(find $HOME/w10/snapshot -type f | wc -l) files)"
  (cd $T/src_tree && find results/validation cases/*/results tests/data/cases/*/results -type f 2>/dev/null | sort | xargs sha256sum > $T/generated_before.sha256)
  c0=$(date +%s)
  (cd $T/build && nice -n 10 ctest -V -j$J --timeout 7200 > $T/ctest_verbose.txt 2>&1)
  echo "ctest exit $? ($(( $(date +%s) - c0 )) s)"
  cp $T/ctest_verbose.txt $T/ctest_verbose_${PREFIX}.txt
  grep -E "tests passed|tests failed out of|Total Test time" $T/ctest_verbose.txt
  sed -n '/The following tests FAILED:/,$p' $T/ctest_verbose.txt | head -30
  (cd $T/src_tree && find results/validation cases/*/results tests/data/cases/*/results -type f 2>/dev/null | sort | xargs sha256sum > $T/generated_after.sha256)
  echo "# generated files before $(wc -l < $T/generated_before.sha256), after $(wc -l < $T/generated_after.sha256), changed $(diff $T/generated_before.sha256 $T/generated_after.sha256 | grep -c '^>')"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG

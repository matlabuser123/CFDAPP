#!/usr/bin/env bash
# P12-GRAD-002 A2 fresh execution, steps 3 and 4 of acceptance_gate_A2.md section 7 (after the
# freeze and after the test file is in the build). Every log is written under a "fresh_" name, so
# the dry-run evidence (dry_*, suite_*, prod_*) stays unchanged.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$R/results/p12-grad-002/a2/tools
L=$R/results/p12-grad-002/a2/logs
[ -e $L/00_freeze.log ] || { echo "REFUSED: A2 is not frozen"; exit 1; }
STEP=${1:-all}
if [ "$STEP" = all ] || [ "$STEP" = probes ]; then
  cd $R
  for tree in repo nograd grad001; do bash $T/run_a2.sh a2_3d fresh_3d_$tree.log $tree > /dev/null & done
  wait
  for tree in repo nograd grad001; do bash $T/run_a2.sh a2_c9 fresh_c9_$tree.log $tree > /dev/null & done
  wait
  W=$HOME/g2/c10_fresh; mkdir -p $W
  for tree in repo nograd base; do bash $T/run_a2.sh a2_c10 tmpf_c10_$tree.log $tree > /dev/null & done
  wait
  for tree in repo nograd base; do
    { head -3 $L/tmpf_c10_$tree.log; tail -2 $L/tmpf_c10_$tree.log; echo "dump lines $(wc -l < $L/tmpf_c10_$tree.log) sha256 $(sha256sum $L/tmpf_c10_$tree.log | cut -d' ' -f1)"; } > $L/fresh_c10_dump_$tree.header.log
    mv $L/tmpf_c10_$tree.log $W/dump_$tree.txt
  done
  python3 $T/compare_c10.py $W/dump_repo.txt $W/dump_nograd.txt > $L/fresh_c10_repo_vs_nograd.log 2>&1; echo "exit $?" >> $L/fresh_c10_repo_vs_nograd.log
  python3 $T/compare_c10.py $W/dump_nograd.txt $W/dump_base.txt > $L/fresh_c10_nograd_vs_base.log 2>&1; echo "exit $?" >> $L/fresh_c10_nograd_vs_base.log
  python3 $T/compare_c10.py $W/dump_repo.txt $W/dump_nograd.txt --mutate-new > $L/fresh_c10_selftest_mutated.log 2>&1; echo "exit $?" >> $L/fresh_c10_selftest_mutated.log
  bash $T/run_a2.sh a2_prodmesh fresh_prodmesh_repo.log repo > /dev/null &
  bash $T/run_a2.sh a2_prodmesh fresh_prodmesh_nograd.log nograd > /dev/null &
  wait
  bash $T/prod_accuracy.sh fresh_prod > /dev/null
  echo "probes done"
fi
if [ "$STEP" = all ] || [ "$STEP" = rebuild ]; then
  {
    echo "# fresh rebuild of cur and nograd with the repository's tests/ and cases/; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    for tree in cur nograd; do
      S=$HOME/g2/$tree/src_tree
      before=$(sha256sum $HOME/g2/$tree/build/src/libcfdcore.a | cut -d' ' -f1)
      rsync -a --delete $R/tests/ $S/tests/
      rsync -a --delete --exclude results $R/cases/ $S/cases/
      echo "$tree: test file $(sha256sum $S/tests/unit/discretization/test_gradient_boundary_consistency.cpp | cut -c1-16), CMakeLists $(sha256sum $S/tests/unit/discretization/CMakeLists.txt | cut -c1-16)"
      echo "$tree: files differing from the repo under src/ include/ tests/: $(cd $S && for f in $(find src include tests -type f); do cmp -s "$f" "$R/$f" || echo "$f"; done | tr '
' ' ')"
      nice -n 10 cmake --build $HOME/g2/$tree/build -j8 > $HOME/g2/$tree/rebuild.txt 2>&1 || { echo "$tree BUILD FAILED"; tail -20 $HOME/g2/$tree/rebuild.txt; exit 1; }
      after=$(sha256sum $HOME/g2/$tree/build/src/libcfdcore.a | cut -d' ' -f1)
      echo "$tree: build OK, warnings $(grep -c 'warning:' $HOME/g2/$tree/rebuild.txt); libcfdcore.a $before -> $after $([ "$before" = "$after" ] && echo UNCHANGED || echo CHANGED)"
      [ "$before" = "$after" ] || exit 1
    done
  } > $L/fresh_rebuild.log 2>&1 || { cat $L/fresh_rebuild.log; exit 1; }
  cat $L/fresh_rebuild.log
fi
if [ "$STEP" = all ] || [ "$STEP" = suites ]; then
  bash $T/run_suite.sh cur 6 fresh_suite > /dev/null &
  bash $T/run_suite.sh nograd 6 fresh_suite > /dev/null &
  wait
  bash $T/dry_c8_c11.sh fresh > /dev/null
  echo "suites done"
fi

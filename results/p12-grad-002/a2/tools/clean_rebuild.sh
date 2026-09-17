#!/usr/bin/env bash
# A2 step 4a (added after the freeze, disclosed): clean-first rebuild of cur and nograd so that every
# executable is newer than every (rsync-copied, mtime-preserving) source file. The frozen
# run_suite.sh fails closed otherwise (first fresh attempt: *.INVALID-stale-check-failed-closed-no-ctest.log).
# The libraries must keep their hashes (deterministic archives).
set -u
L=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/a2/logs/fresh_clean_rebuild.log
{
  echo "# clean-first rebuild of cur and nograd; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  for tree in cur nograd; do
    B=$HOME/g2/$tree/build
    before=$(sha256sum $B/src/libcfdcore.a | cut -d' ' -f1)
    t0=$(date +%s)
    nice -n 5 cmake --build $B -j16 --clean-first > $HOME/g2/$tree/clean_rebuild.txt 2>&1 || { echo "$tree BUILD FAILED"; tail -20 $HOME/g2/$tree/clean_rebuild.txt; exit 1; }
    after=$(sha256sum $B/src/libcfdcore.a | cut -d' ' -f1)
    echo "$tree: $(( $(date +%s) - t0 )) s, warnings $(grep -c 'warning:' $HOME/g2/$tree/clean_rebuild.txt); libcfdcore.a $before -> $after $([ "$before" = "$after" ] && echo UNCHANGED || echo CHANGED)"
    [ "$before" = "$after" ] || exit 1
  done
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L 2>&1
cat $L

#!/usr/bin/env bash
# P12-GRAD-002 A3, after run_regression.sh stage 6 (housekeeping, not a criterion; disclosed):
# stage 6 restores every generated file whose pre-regression snapshot is value-identical to the
# Release-stage output, and KEEPS the rest. The kept files are those whose snapshot was written by
# a sanitizer (Debug) build -- DIFF-002 W10's stage-0 snapshot did not cover cases/*/results and
# tests/data/cases/*/results, so their last writer was W10's ASan stage -- and the regression's own
# ASan stage wrote them last again. Leave each of them equal to the authoritative Release-stage
# output of the current code instead of a sanitizer build's.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
W=$HOME/g2/regr
LOG=$R/results/p12-grad-002/a3/logs/regr_07_finalize_outputs.log
cd $R || exit 1
{
  echo "# finalize generated outputs $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  n=0
  grep '^KEPT' $P/logs/regr_06_restore.log | sed 's/^KEPT (Release values differ from the snapshot): //' | while read -r f; do
    if [ -f "$W/release_outputs/$f" ]; then
      printf '%s\n' "$f" > $W/one.txt
      echo "== $f: snapshot vs Release-stage output:"
      python3 $P/tools/classify_scope.py "$W/snapshot" "$W/release_outputs" $W/one.txt | head -4 | sed 's/^/   /'
      cp -p "$W/release_outputs/$f" "$f" && echo "   -> set to the Release-stage output"
    else
      echo "== $f: no Release-stage copy -- left unchanged"
    fi
  done
  echo "working-tree generated files differing from the Release-stage outputs afterwards: $(cd $W/release_outputs && find . -type f | while read -r f; do cmp -s "$f" "$R/$f" || echo "$f"; done | wc -l) (timing-only restores from the snapshot are expected here)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG

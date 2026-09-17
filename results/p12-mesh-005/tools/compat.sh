#!/usr/bin/env bash
# P12-MESH-005 gate item G3 -- 2D backward compatibility of the CLI:
#   BASE = the working tree before any MESH-005 change (MESH-001..004 state), copied to $HOME/m5ref/base
#          and built there in Release (m5_snapshot.sh);
#   NEW  = the current working tree, build/release.
# Every committed case with a case.json and every CLI fixture is run with both. Exported files
# (fields.csv, solution.vtk, residuals.csv) are compared byte for byte, metadata.json as parsed JSON (all
# keys), stdout line by line (the lines naming the per-run case directory removed). Any difference is
# quantified.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
NEW=$R/build/release/apps/cli/cfdapp
BASE=$HOME/m5ref/base/build/apps/cli/cfdapp
echo "HEAD $(cd $R && git rev-parse HEAD); NEW cfdapp sha256 $(sha256sum $NEW | cut -c1-16); BASE cfdapp sha256 $(sha256sum $BASE | cut -c1-16)"
W=$HOME/m5compat; rm -rf $W; mkdir -p $W
runone() { # binary tag src name
  mkdir -p $W/$2; rm -rf $W/$2/$4; cp -r $3 $W/$2/$4; rm -rf $W/$2/$4/results
  (cd $HOME && $1 --case $W/$2/$4 > $W/$2/$4.stdout 2>&1; echo "exit $?" >> $W/$2/$4.stdout)
}
clean_stdout() { grep -v "$W" $1; }
compare() { # name
  local v="IDENTICAL"
  local files=0
  for f in fields.csv solution.vtk residuals.csv; do
    if [ ! -e $W/base/$1/results/$f ] && [ ! -e $W/new/$1/results/$f ]; then continue; fi
    files=$((files+1))
    cmp -s $W/base/$1/results/$f $W/new/$1/results/$f || v="DIFFERENT($f)"
  done
  python3 - "$W/base/$1/results/metadata.json" "$W/new/$1/results/metadata.json" <<'PY' || v="$v DIFFERENT(metadata)"
import json, sys
def load(p):
    try:
        return json.load(open(p))
    except FileNotFoundError:
        return None
sys.exit(0 if load(sys.argv[1]) == load(sys.argv[2]) else 1)
PY
  a=$(clean_stdout $W/base/$1.stdout | md5sum | cut -c1-8); b=$(clean_stdout $W/new/$1.stdout | md5sum | cut -c1-8)
  [ "$a" = "$b" ] || v="$v DIFFERENT(stdout)"
  echo "base vs new  $1: $v | files compared $files | $(tail -1 $W/base/$1.stdout)/$(tail -1 $W/new/$1.stdout)"
  case "$v" in IDENTICAL*) ;; *) diff <(clean_stdout $W/base/$1.stdout) <(clean_stdout $W/new/$1.stdout) | head -10 | sed 's/^/      /' ;; esac
}
CASES=""
for c in $R/cases/*; do [ -e $c/case.json ] && CASES="$CASES $(basename $c)"; done
FIXTURES=""
for c in $R/tests/data/cases/*; do [ -e $c/case.json ] && FIXTURES="$FIXTURES $(basename $c)"; done
for c in $CASES; do (runone $NEW new $R/cases/$c $c; runone $BASE base $R/cases/$c $c) & done
for c in $FIXTURES; do (runone $NEW new $R/tests/data/cases/$c data_$c; runone $BASE base $R/tests/data/cases/$c data_$c) & done
wait
echo "=== committed cases ($(echo $CASES | wc -w))"
for c in $CASES; do compare $c; done
echo "=== CLI data fixtures ($(echo $FIXTURES | wc -w))"
for c in $FIXTURES; do compare data_$c; done
identical=$( { for c in $CASES; do compare $c; done; for c in $FIXTURES; do compare data_$c; done; } | grep -c ': IDENTICAL')
echo "=== totals: $identical IDENTICAL of $(( $(echo $CASES | wc -w) + $(echo $FIXTURES | wc -w) ))"

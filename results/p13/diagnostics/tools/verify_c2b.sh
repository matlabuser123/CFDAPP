#!/usr/bin/env bash
# C2b -- stdout must be byte-identical at EVERY --log-level, for all 7 cases.
# This is the DETECTION counterpart of C2: C2 pins stdout at the default level
# (where, after amendment A1, nothing is emitted to disturb it), C2b pins it at
# the levels where diagnostics actually flow.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BASE=results/p13/diagnostics/data/baseline-output
TOT=0
for lvl in error warning info debug trace; do
  O=/tmp/p13_c2b_$lvl; rm -rf $O
  bash results/p13/diagnostics/tools/cli_output_corpus.sh $O --log-level $lvl >/dev/null 2>&1
  d=0
  for f in "$BASE"/*.stdout; do
    diff -q "$f" "$O/$(basename $f)" >/dev/null 2>&1 || { d=$((d+1)); }
  done
  echo "  --log-level $lvl : stdout files differing from the default baseline = $d"
  TOT=$((TOT+d))
done
echo "C2b total differing: $TOT"

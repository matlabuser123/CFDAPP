#!/usr/bin/env bash
# P12-MESH-006: for each formatted file whose whitespace-stripped token stream changed, show the
# diff and classify it (comment re-wrap, string-literal split, other).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$HOME/m6format_pre
cd $R
for f in apps/cli/main.cpp src/pressure_velocity/SIMPLE.cpp tests/integration/case/test_3d_production_cases.cpp \
         tests/integration/mms/test_mms_simple3d.cpp tests/solver/simple/test_simple3d.cpp; do
  echo "=== $f"
  # compare with comments removed and adjacent string literals joined: must be identical
  strip() { sed -E 's#//.*$##' "$1" | tr -d ' \t\n' | sed -E 's/""//g'; }
  if cmp -s <(strip $P/$f) <(strip $f); then
    echo "  identical after removing comments, whitespace and string-literal splits (\"\" joins)"
  else
    echo "  STILL DIFFERENT after removing comments/whitespace/literal splits:"
    diff $P/$f $f | head -20
  fi
done
echo "=== full diff of the two production files"
for f in apps/cli/main.cpp src/pressure_velocity/SIMPLE.cpp; do diff $P/$f $f; done

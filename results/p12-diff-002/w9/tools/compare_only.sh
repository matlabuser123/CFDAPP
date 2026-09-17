#!/usr/bin/env bash
# W9 / W9A: re-evaluate an existing set of W9 runs (no solving). usage: compare_only.sh <runs-root> <log-name>
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$R" || exit 1
CASES=$(cd "$R/cases" && for d in */; do [ -f "$d/case.json" ] && echo "${d%/}"; done)
python3 "$R/results/p12-diff-002/w9/tools/w9_compare.py" "$1" $CASES > "$R/results/p12-diff-002/w9/logs/$2" 2>&1
echo "compare exit $?" >> "$R/results/p12-diff-002/w9/logs/$2"

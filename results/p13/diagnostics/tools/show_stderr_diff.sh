#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BASE=results/p13/diagnostics/data/baseline-output
AFTER=/tmp/p13_diag_after
for f in "$BASE"/*.stderr; do
  b=$(basename "$f")
  echo "######## $b   (baseline $(wc -c <"$f") bytes -> after $(wc -c <"$AFTER/$b") bytes)"
  diff "$f" "$AFTER/$b" | head -12
done

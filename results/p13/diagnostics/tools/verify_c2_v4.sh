#!/usr/bin/env bash
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
cmake --build build/final -j 12 2>&1 | tail -3
echo
echo "=== C2/V4: re-capture the corpus with the changed binary ==="
bash results/p13/diagnostics/tools/cli_output_corpus.sh /tmp/p13_diag_after >/dev/null 2>&1
BASE=results/p13/diagnostics/data/baseline-output
AFTER=/tmp/p13_diag_after
sd=0; se=0; n=0
for f in "$BASE"/*.stdout; do
  b=$(basename "$f"); n=$((n+1))
  if ! diff -q "$f" "$AFTER/$b" >/dev/null 2>&1; then echo "STDOUT DIFFERS: $b"; sd=$((sd+1)); fi
done
for f in "$BASE"/*.stderr; do
  b=$(basename "$f")
  if ! diff -q "$f" "$AFTER/$b" >/dev/null 2>&1; then echo "STDERR DIFFERS: $b"; se=$((se+1)); fi
done
echo "cases: $n   stdout differing: $sd   stderr differing: $se"
echo "C2 (stdout byte-identical): $([ $sd -eq 0 ] && echo PASS || echo FAIL)"
echo "V4 (default stderr unchanged): $([ $se -eq 0 ] && echo PASS || echo FAIL)"

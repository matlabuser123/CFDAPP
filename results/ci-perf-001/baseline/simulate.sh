#!/usr/bin/env bash
# CI-PERF-001: simulate shard balance against the runtimes and the exact
# `ctest -N` listing recorded from run 35352132866 (1977 listed = 1932 executed
# + 45 disabled).  Serial load is reported; the runners execute with -j$(nproc).
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

LIST=results/ci-perf-001/baseline/ci_ctest_full_list.txt
NPROC=4 # github-hosted ubuntu-latest

echo "list: $(grep -cE '^ *Test +#' "$LIST") tests (as CI sees it)"
echo

for cfg in clang_debug gcc_debug asan; do
  for n in 1 2 3 4 5 6; do
    out=$(python3 scripts/ci/shard_tests.py \
            --list-file "$LIST" --config "$cfg" --shards "$n" --check 2>&1)
    cov=$(echo "$out" | grep -o 'COMPLETE, each test in exactly one shard\|BROKEN')
    maxmin=$(echo "$out" | sed -n 's/.*max \([0-9.]*\) min.*/\1/p')
    spread=$(echo "$out" | sed -n 's/.*spread \([0-9.]*\)x.*/\1/p')
    floor=$(echo "$out" | sed -n 's/single-test floor: \([0-9.]*\) min/\1/p')
    # wall estimate = max(heaviest single test, shard serial load / nproc) + build
    wall=$(python3 -c "
import sys
m,f = float('$maxmin'), float('$floor')
print('%.1f' % max(f, m/$NPROC))")
    printf '%-12s shards=%d  serial/shard %6s min  ->  est. wall %5s min  (floor %5s, spread %s)  %s\n' \
      "$cfg" "$n" "$maxmin" "$wall" "$floor" "${spread}x" "$cov"
  done
  echo
done

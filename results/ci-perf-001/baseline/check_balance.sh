#!/usr/bin/env bash
# CI-PERF-001: final predicted shard balance, against the exact `ctest -N`
# listing CI produced on run 35352132866.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
LIST=results/ci-perf-001/baseline/ci_ctest_full_list.txt

show() { # config shards
  echo "=== $1, $2 shards ==="
  python3 scripts/ci/shard_tests.py --list-file "$LIST" --config "$1" --shards "$2" --check \
    | grep -E 'listed =|predicted|floor|shard [0-9]+/|coverage'
  echo
}

show clang_debug 3
show gcc_debug 3
show asan 5

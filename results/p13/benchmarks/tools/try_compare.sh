#!/usr/bin/env bash
# First exercise of the comparator: a run against ITSELF (must pass, and must
# report a nonzero number of comparisons -- comparing nothing is a FAIL).
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd /tmp
B=$ROOT/build/final/benchmarks/cpu/cfd_benchmark_cpu
rm -rf /tmp/p13_base /tmp/p13_curr
$B --output-dir /tmp/p13_base > /dev/null 2>&1
$B --output-dir /tmp/p13_curr > /dev/null 2>&1
echo "=== a fresh run vs a fresh baseline (identical binary) ==="
python3 $ROOT/scripts/benchmarks/compare_benchmarks.py --baseline /tmp/p13_base --current /tmp/p13_curr
echo "exit: $?"

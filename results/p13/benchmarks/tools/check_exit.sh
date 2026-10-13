#!/usr/bin/env bash
# The previous check read `| head -2` then echoed $? -- head's status, not the
# benchmark's. Same class of error as P13-005's `| tail -N; echo RC=$?`.
cd /tmp
B=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/build/final/benchmarks/cpu/cfd_benchmark_cpu
$B --output-dir > /tmp/p13_missing.log 2>&1
echo "missing argument -> exit $?"
cat /tmp/p13_missing.log
$B --output-dir /proc/cannot/create/this > /tmp/p13_badpath.log 2>&1
echo "uncreatable path -> exit $?"
head -1 /tmp/p13_badpath.log

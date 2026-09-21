#!/usr/bin/env bash
# GPU-DISC-001J -- reconfigure + build, printing only diagnostics.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cmake -S . -B build/cuda > /tmp/cmake.log 2>&1 || { tail -20 /tmp/cmake.log; exit 1; }
ninja -C build/cuda > /tmp/ninja.log 2>&1
RC=$?
grep -vE '^\[[0-9]+/[0-9]+\]' /tmp/ninja.log | head -60
echo "BUILD RC=$RC"
exit $RC

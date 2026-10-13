#!/usr/bin/env bash
# Compile just the core library, to get P17.7's compile errors fast.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
B="${1:-build/qual-final}"
T="${2:-cfdcore}"
cmake --build "$B" --target "$T" -j 16 > /tmp/p17_7_build.log 2>&1
rc=$?
echo "build exit: $rc   ($B / $T)"
grep -aE '(^|[^a-zA-Z])error:' /tmp/p17_7_build.log | head -25
grep -aE 'warning:' /tmp/p17_7_build.log | grep -ai hllc | head -10
grep -aE 'Built target|Hllc' /tmp/p17_7_build.log | head -8
exit $rc

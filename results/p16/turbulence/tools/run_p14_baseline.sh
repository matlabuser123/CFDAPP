#!/usr/bin/env bash
# P14 migration, step 1: the baseline gate. If this is red, the migration does
# not start -- a migration proved against a red baseline proves nothing.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
B=./build/final/tests/unit/transport/CFDTransportTests
rm -f "$B"
cmake --build build/final --target CFDTransportTests -j 16 2>&1 | grep -E "error:|FAILED" | head -20
[ -x "$B" ] || { echo "BUILD FAILED"; exit 1; }
echo "BUILD OK"
"$B" 2>&1 | tail -12

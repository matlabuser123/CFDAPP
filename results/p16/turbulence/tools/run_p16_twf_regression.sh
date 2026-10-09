#!/usr/bin/env bash
# P16-TWF closeout regression, ordered so a failure localises.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
cmake --build build/final -j 16 2>&1 | grep -E "error:|FAILED" | head -20
for t in thermal turbulence io solver physics transport; do
  B=$(find ./build/final/tests/unit/$t -maxdepth 1 -name 'CFD*Tests' -type f 2>/dev/null | head -1)
  [ -x "$B" ] || { printf '%-14s NOT BUILT\n' "$t"; continue; }
  line=$("$B" 2>&1 | grep -E "^\[==========\] [0-9]+ tests? from" | tail -1)
  fails=$("$B" 2>&1 | grep -cE "^\[  FAILED  \]")
  printf '%-14s %-48s failures=%s\n' "$t" "$line" "$fails"
done
echo "--- case integration (ProjectRunner) ---"
./build/final/tests/integration/case/CFDCaseIntegrationTests 2>&1 | grep -E "^\[==========\] [0-9]+ tests|^\[  PASSED|^\[  FAILED" | tail -4

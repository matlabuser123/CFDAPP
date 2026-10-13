#!/usr/bin/env bash
# P14 migration closeout: every suite the migration could have touched.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
fail=0
for t in transport turbulence io solver physics thermal discretization algebra; do
  B=$(find ./build/final/tests/unit/$t -maxdepth 1 -name 'CFD*Tests' -type f 2>/dev/null | head -1)
  [ -x "$B" ] || { printf '%-16s %s\n' "$t" "NOT BUILT"; continue; }
  line=$("$B" 2>&1 | grep -E "^\[==========\] [0-9]+ tests? from" | tail -1)
  res=$("$B" 2>&1 | grep -cE "^\[  FAILED  \]")
  printf '%-16s %-46s failures=%s\n' "$t" "$line" "$res"
  [ "$res" != "0" ] && fail=1
done
echo
echo "=== determinism: repeat the frozen oracles ==="
for i in 1 2 3; do
  ./build/final/tests/unit/turbulence/CFDTurbulenceTests \
    --gtest_filter='P14Migration.*' 2>&1 | grep -E "^\[  PASSED  \]" | sed "s/^/  run $i: /"
done
exit $fail

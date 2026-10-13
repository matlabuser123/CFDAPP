#!/usr/bin/env bash
# After clang-format -i: prove the reformat changed NOTHING numerically.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/hllc
cmake --build build/qual-final --target CFDCompressibleTests CFDGpuTests -j 16 \
  > "$P17/logs/08_postformat_build.log" 2>&1
echo "build exit: $?"
OMP_NUM_THREADS=1 ctest --test-dir build/qual-final -j 8 -R Hllc \
  > "$P17/logs/08_postformat.log" 2>&1
echo "ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/08_postformat.log" | tail -1 | sed 's/^/  /'
echo ""
echo "the measured values, to compare against logs/03_hllc_measured.log:"
./build/qual-final/tests/unit/compressible/CFDCompressibleTests --gtest_filter='Hllc*' \
  > "$P17/logs/08_postformat_measured.log" 2>&1
grep -aE 'worst|W4:|W3 |T1 stationary|T4 contact|X1 worst|X2 worst|Galilean|16B' \
  "$P17/logs/08_postformat_measured.log" | sed 's/^ */  /'
echo ""
echo "DIFF against the pre-format measurements (empty = the reformat moved no number):"
diff <(grep -aE 'worst|W4:|W3 |T1 |T4 |X1 |X2 ' "$P17/logs/03_hllc_measured.log") \
     <(grep -aE 'worst|W4:|W3 |T1 |T4 |X1 |X2 ' "$P17/logs/08_postformat_measured.log") \
  && echo "  EMPTY -- identical"

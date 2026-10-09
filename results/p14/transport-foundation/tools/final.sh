#!/usr/bin/env bash
# P14 items 11-13: CUDA ON/OFF + Debug/Release, full CTest, SIMPLE fingerprint.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
R=results/p14/transport-foundation
echo "date: $(date -u +%FT%TZ)"

echo
echo "=== G: clang-format-18 at CI scope ==="
n=0
for f in $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \)); do
  clang-format-18 --style=file "$f" | diff -q - "$f" >/dev/null || { echo "  FORMAT: $f"; n=$((n+1)); }
done
echo "  violations: $n"

run() { # run <label> <dir>
  echo; echo "=== $1 ($2) ==="
  cmake --build "$2" -j 12 2>&1 | grep -E "error:|FAILED" | head -10
  cmake --build "$2" -j 12 2>&1 | tail -1
  ( cd "$2" && ctest -j 4 ) > "$R/logs/ctest_$1.log" 2>&1
  echo "  ctest exit status: $?"
  grep -a "tests passed\|tests failed" "$R/logs/ctest_$1.log" | tail -2 | sed 's/^/  /'
  grep -a "The following tests FAILED" -A 15 "$R/logs/ctest_$1.log" | head -17 | sed 's/^/  /'
  echo "  transport tests in this suite: $(grep -ac 'Transport' "$R/logs/ctest_$1.log")"
}
run release_cuda build/final
run debug_gui    build/debug
run cpu_only     build/cpuonly

echo
echo "p14_regression_done: $(date -u +%FT%TZ)"

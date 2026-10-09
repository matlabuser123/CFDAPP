#!/usr/bin/env bash
# Combined P14 + P15 regression. See
# results/p14/transport-foundation/logs/REGRESSION_DEFERRED.md for why P14's is
# folded in here rather than run separately.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
R=results/p15/thermal-energy
echo "date: $(date -u +%FT%TZ)"

echo
echo "=== clang-format-18 at CI scope ==="
n=0
for f in $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \)); do
  clang-format-18 --style=file "$f" | diff -q - "$f" >/dev/null || { echo "  FORMAT: $f"; n=$((n+1)); }
done
echo "  violations: $n"

run() {
  echo; echo "=== $1 ($2) ==="
  if ! cmake --build "$2" -j 12 > "$R/logs/build_$1.log" 2>&1; then
    echo "  BUILD FAILED"; grep -E "error:" "$R/logs/build_$1.log" | head -10; return 1
  fi
  echo "  build ok"
  ( cd "$2" && ctest -j 4 ) > "$R/logs/ctest_$1.log" 2>&1
  echo "  ctest exit status: $?"
  grep -a "tests passed\|tests failed" "$R/logs/ctest_$1.log" | tail -2 | sed 's/^/  /'
  grep -a "The following tests FAILED" -A 15 "$R/logs/ctest_$1.log" | head -17 | sed 's/^/  /'
}
run release_cuda build/final
run debug_gui    build/debug
run cpu_only     build/cpuonly

echo
echo "=== P13-001 SIMPLE fingerprint (must stay IDENTICAL) ==="
bash results/p14/transport-foundation/tools/fingerprint.sh 2>&1 | tail -4

echo
echo "combined_regression_done: $(date -u +%FT%TZ)"

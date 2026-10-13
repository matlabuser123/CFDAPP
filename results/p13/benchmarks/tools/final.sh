#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
R=results/p13/benchmarks
echo "date: $(date -u +%FT%TZ)"
echo
echo "=== G: clang-format-18 at CI scope ==="
n=0
for f in $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \)); do
  clang-format-18 --style=file "$f" | diff -q - "$f" >/dev/null || { echo "  FORMAT: $f"; n=$((n+1)); }
done
echo "  violations: $n"
echo
echo "=== benchmarks/ format (not in the CI scope, checked anyway) ==="
m=0
for f in benchmarks/*.hpp benchmarks/*/*.cpp; do
  clang-format-18 --style=file "$f" | diff -q - "$f" >/dev/null || { echo "  FORMAT: $f"; m=$((m+1)); }
done
echo "  violations: $m"
run() {
  echo; echo "=== P2: $1 ($2) ==="
  cmake --build "$2" -j 12 2>&1 | tail -1
  ( cd "$2" && ctest -j 4 ) > "$R/logs/ctest_$1.log" 2>&1
  echo "  ctest exit status: $?"
  grep -a "tests passed\|tests failed" "$R/logs/ctest_$1.log" | tail -2 | sed 's/^/  /'
  grep -a "The following tests FAILED" -A 15 "$R/logs/ctest_$1.log" | head -17 | sed 's/^/  /'
  grep -a "BenchmarkSuiteSmoke" "$R/logs/ctest_$1.log" | tail -1 | sed 's/^/  /'
}
run release_cuda build/final
run debug_gui    build/debug
run cpu_only     build/cpuonly
echo
echo "bench_done: $(date -u +%FT%TZ)"

#!/usr/bin/env bash
# P15 FINAL regression -- all three configurations, the whole suite, at the
# measured parallelism (parallelism.md: OpenMP OFF so ctest -j cannot
# oversubscribe; the floor is the longest single test, ~796.7 s in Debug).
#
# Run on the closed tree. No production source may change while this runs.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
R=results/p15/thermal-energy
JOBS=16
mkdir -p "$R/logs/final"
echo "P15 FINAL REGRESSION"
echo "date: $(date -u +%FT%TZ)   ctest -j $JOBS"
echo "gpu: $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>&1 | head -1)"
echo "nvcc: $(nvcc --version 2>&1 | grep release)"

echo
echo "=== clang-format-18 at CI scope ==="
n=0
for f in $(find include src apps tests cuda \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cu' \)); do
  clang-format-18 --style=file "$f" | diff -q - "$f" >/dev/null || { echo "  FORMAT: $f"; n=$((n+1)); }
done
echo "  violations: $n"

run() {
  echo; echo "=== $1 ($2) ==="
  local t0=$(date +%s)
  if ! cmake --build "$2" -j 20 > "$R/logs/final/build_$1.log" 2>&1; then
    echo "  BUILD FAILED"; grep -E "error:" "$R/logs/final/build_$1.log" | head -10; return 1
  fi
  echo "  build ok ($(( $(date +%s) - t0 )) s)"
  t0=$(date +%s)
  ( cd "$2" && ctest -j $JOBS ) > "$R/logs/final/ctest_$1.log" 2>&1
  local rc=$?
  echo "  ctest exit status: $rc, wall $(( $(date +%s) - t0 )) s"
  grep -a "tests passed\|tests failed" "$R/logs/final/ctest_$1.log" | tail -2 | sed 's/^/  /'
  grep -a "The following tests FAILED" -A 20 "$R/logs/final/ctest_$1.log" | head -22 | sed 's/^/  /'
}

run release_cuda build/final
run debug_gui    build/debug
run cpu_only     build/cpuonly

echo
echo "=== P13-001 SIMPLE fingerprint (must stay IDENTICAL) ==="
bash results/p14/transport-foundation/tools/fingerprint.sh 2>&1 | tail -5

echo
echo "final_regression_done: $(date -u +%FT%TZ)"

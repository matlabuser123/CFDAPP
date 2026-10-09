#!/usr/bin/env bash
# The two suites the session restart interrupted, at measured parallelism.
#
# PARALLELISM RATIONALE (results/p15/thermal-energy/parallelism.md):
#   OpenMP is OFF in every build config, so tests are single-threaded and
#   ctest -j cannot oversubscribe. ~150 MiB per test against 30 GiB available,
#   so memory is not the constraint either. The floor is the longest single
#   test (796.7 s in Debug), reached at about j=20; j=16 is the conservative
#   setting and already ~4x the previous j=4.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
R=results/p15/thermal-energy
JOBS=16
echo "date: $(date -u +%FT%TZ)   ctest -j $JOBS"

run() {
  echo; echo "=== $1 ($2) ==="
  local t0=$(date +%s)
  if ! cmake --build "$2" -j 20 > "$R/logs/build_$1.log" 2>&1; then
    echo "  BUILD FAILED"; grep -E "error:" "$R/logs/build_$1.log" | head -10; return 1
  fi
  echo "  build ok ($(( $(date +%s) - t0 )) s)"
  t0=$(date +%s)
  ( cd "$2" && ctest -j $JOBS ) > "$R/logs/ctest_$1.log" 2>&1
  local rc=$?
  echo "  ctest exit status: $rc, wall $(( $(date +%s) - t0 )) s"
  grep -a "tests passed\|tests failed" "$R/logs/ctest_$1.log" | tail -2 | sed 's/^/  /'
  grep -a "The following tests FAILED" -A 15 "$R/logs/ctest_$1.log" | head -17 | sed 's/^/  /'
  echo "  peak mem during run: $(free -m | awk '/^Mem:/{print $3" MiB used"}')"
}
run debug_gui build/debug
run cpu_only  build/cpuonly
echo
echo "rest_regression_done: $(date -u +%FT%TZ)"

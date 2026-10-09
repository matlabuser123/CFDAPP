#!/usr/bin/env bash
# P13-005 regression, with the exit status captured correctly.
#
# final.sh got this wrong: `ctest ... | tail -4; echo "RC=$?"` reports TAIL's
# status, not ctest's, so its RC=0 proved nothing. Here ctest's full output is
# written to a file and ${PIPESTATUS[0]} is read, and the summary line is
# printed from the file -- two independent statements of the same result.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
echo "date: $(date -u +%FT%TZ)"
L=results/p13/diagnostics/logs

run() { # run <label> <build-dir>
  echo
  echo "=== $1 ($2) ==="
  cmake --build "$2" -j 12 2>&1 | tail -1
  ( cd "$2" && ctest -j 4 ) > "$L/ctest_$1.log" 2>&1
  rc=$?
  echo "  ctest exit status: $rc"
  grep -a "tests passed\|tests failed" "$L/ctest_$1.log" | tail -2
  grep -a "The following tests FAILED" -A 20 "$L/ctest_$1.log" | head -22
}

run release_cuda build/final
run debug_gui    build/debug
run cpu_only     build/cpuonly
echo
echo "ctest_done: $(date -u +%FT%TZ)"

#!/usr/bin/env bash
# P12-DIFF-002 A4-2: build EVERYTHING, then run the complete ctest suite, so the legacy-assumption
# inventory is taken from ground truth rather than from the subset A3 happened to look at. Uses the
# A4 rule: configure -> build -> verify -> only then execute. Fails closed on a build failure.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
LOG=$R/results/p12-diff-002/a4/logs/01_full_inventory.log
cd $R
{
  echo "# P12-DIFF-002 A4-2 complete inventory run; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo "# configure: cmake -S $R -B $B"
  cmake -S $R -B $B > /dev/null 2>&1 && echo "# configure OK" || echo "# configure FAILED"
  echo "# build all: cmake --build $B -j\$(nproc)"
  if cmake --build $B -j"$(nproc)" > $R/results/p12-diff-002/a4/logs/01_build.log 2>&1; then
    echo "# BUILD OK"
  else
    echo "# BUILD FAILED -- fail closed, nothing executed"
    tail -30 $R/results/p12-diff-002/a4/logs/01_build.log
    exit 1
  fi
  echo "# library $(sha256sum $B/src/libcfdcore.a | cut -c1-16)"
  echo "# run: ctest --test-dir $B -j\$(nproc) --output-on-failure"
  echo
} > $LOG 2>&1
ctest --test-dir $B -j"$(nproc)" > $R/results/p12-diff-002/a4/logs/01_ctest_raw.log 2>&1
{
  echo "## ctest summary"
  grep -E '^ *[0-9]+% tests passed|^Total Test time|tests failed out of' \
    $R/results/p12-diff-002/a4/logs/01_ctest_raw.log
  echo
  echo "## every failing ctest entry"
  grep -E '^\s+[0-9]+ - ' $R/results/p12-diff-002/a4/logs/01_ctest_raw.log | sed 's/^/  /'
} >> $LOG 2>&1
cat $LOG

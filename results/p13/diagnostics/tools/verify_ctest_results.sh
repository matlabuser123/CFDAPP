#!/usr/bin/env bash
# P13-005 -- read each suite's result from ctest's OWN per-test record, rather
# than from final.sh's broken `| tail -4; echo RC=$?` (which reports tail's
# status, not ctest's, and is 0 whether or not tests failed).
#
# ctest writes Testing/Temporary/LastTest.log as it runs, one "Test Passed." /
# "Test Failed" per test, and writes LastTestsFailed.log ONLY when something
# failed. Counting the per-test records is stronger evidence than a summary
# line: it says how many tests actually ran, not just that none failed.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
for b in build/final build/debug build/cpuonly; do
  T=$b/Testing/Temporary
  echo "=== $b ==="
  if [ ! -f "$T/LastTest.log" ]; then echo "  no LastTest.log"; continue; fi
  echo "  LastTest.log         : $(date -r "$T/LastTest.log" -u +%FT%TZ)"
  echo "  Test Passed. records : $(grep -a -c 'Test Passed\.' "$T/LastTest.log")"
  echo "  Test Failed records  : $(grep -a -c 'Test Failed' "$T/LastTest.log")"
  echo "  '(Failed)' markers   : $(grep -a -c '(Failed)' "$T/LastTest.log")"
  if [ -f "$T/LastTestsFailed.log" ]; then
    echo "  LastTestsFailed.log  : $(date -r "$T/LastTestsFailed.log" -u +%FT%TZ)  <- older than LastTest.log means it is from an EARLIER run"
    sed 's/^/      /' "$T/LastTestsFailed.log"
  else
    echo "  LastTestsFailed.log  : absent (ctest writes it only when a test fails)"
  fi
done

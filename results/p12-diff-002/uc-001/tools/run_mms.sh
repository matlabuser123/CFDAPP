#!/usr/bin/env bash
# UC-001 Step 10: the MMS validation suite. The freshly linked binaries on the Windows mount can
# come out without the executable bit, so set it explicitly before running.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uc-001
mkdir -p "$P/logs"
cd "$R" || exit 1
BIN=build/release/tests/integration/mms/CFDMMSValidationTests
chmod +x "$BIN" || exit 1
{
  echo "# UC-001 Step 10 MMS suite; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore.a sha256 $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  ./"$BIN"
  echo "exit $?"
} > "$P/logs/11_mms.log" 2>&1
grep -E "^\[==========\]|^\[  PASSED  \]|^\[  FAILED  \]|^exit " "$P/logs/11_mms.log"

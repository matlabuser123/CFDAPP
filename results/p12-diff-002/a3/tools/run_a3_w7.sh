#!/usr/bin/env bash
# P12-DIFF-002 W7: focused verification from P12-NUM, MESH-001..006 and GRAD-002, each suite against
# ITS OWN originally frozen thresholds -- no test, threshold or golden output is modified. The suite
# list and the one-line-per-suite format deliberately mirror
# results/p12-grad-002/a1/logs/10_focused_new.log so the two logs are directly comparable.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
L=$P/a3/logs/06_A3_6_fresh_W7.log
cd $R
SUITES="CFDDiscretizationTests CFDMeshTests CFDPisoTests CFDSolverTests CFDCoreTests CFDFieldTests
         CFDAlgebraTests CFDThermalTests CFDTurbulenceTests CFDMMSValidationTests
         CFDCaseIntegrationTests"
{
  echo "# P12-DIFF-002 A3-6 FRESH complete W7 suites; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore $(sha256sum build/release/src/libcfdcore.a | cut -c1-16)"
  echo "# baseline for comparison: results/p12-grad-002/a1/logs/10_focused_new.log (pre-DIFF-002)"
  for s in $SUITES; do
    BIN=$(find build/release -type f -name "$s" -perm -u+x 2>/dev/null | head -1)
    if [ -z "$BIN" ]; then
      cmake --build build/release --target "$s" -j"$(nproc)" > /dev/null 2>&1
      BIN=$(find build/release -type f -name "$s" -perm -u+x 2>/dev/null | head -1)
    fi
    if [ -z "$BIN" ]; then
      echo "$s: NOT BUILT"
      continue
    fi
    OUT=$("$BIN" 2>&1)
    echo "$s: $(echo "$OUT" | grep -E '^\[  FAILED  \]|^\[==========\] [0-9]+ tests from|^\[  PASSED  \]' | tr '\n' ' ')"
  done
} > $L 2>&1
cat $L

#!/bin/bash
# TD-032 -- C7's SECOND CLAUSE: no test breaches the repository cap of ctest --timeout 7200.
#
# C6 deliberately EXCLUDES G8PremixedFlameTest, so stage D measured nothing about the cost of the
# tests that actually drive the premixed flame -- which are exactly the ones whose cost this fix
# could change. C7 cannot be reported as PASS while half of it is unmeasured.
#
# WHAT IS AND IS NOT CLAIMED HERE. This run measures WALL TIME. G8PremixedFlameTest is TD-004
# territory and most of its tests FAIL by its own record; the fix moves every field-derived
# quantity, so those values WILL change. Per GATE.md section 4 that re-measurement belongs to
# TD-004's gate, not to TD-032, and the pass/fail movement below is recorded as information for
# TD-004 and is NOT claimed as TD-032 evidence in either direction.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
echo "date   $(date -Is)"
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$BUILD/tests/unit/species/CFDSpeciesTests"
echo
echo "=== G8PremixedFlameTest, per-test timings. Repository cap is 7200 s PER TEST CASE."
start=$(date +%s)
"$BUILD/tests/unit/species/CFDSpeciesTests" --gtest_filter=G8PremixedFlameTest.* 2>&1
echo
echo "total elapsed $(( $(date +%s) - start )) s"
echo "DONE $(date -Is)"

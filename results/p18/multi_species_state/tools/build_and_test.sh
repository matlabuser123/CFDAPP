#!/usr/bin/env bash
# P18.1 -- focused build + test of the multi-species state.
#
# Deletes the test binary FIRST and requires it back, so a compile failure
# can never leave a stale binary that then "passes" (the stale-binary class
# this project has hit three times; see results/p15's control harness).
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1

BUILD=${1:-build/final}
BIN=$BUILD/tests/unit/species/CFDSpeciesTests

echo "=== configuration under test ==="
grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_BUILD_GUI|CFDAPP_ENABLE_CUDA|CFDAPP_ENABLE_OPENMP):' \
  "$BUILD/CMakeCache.txt" | sed 's/^/  /'
echo ""

rm -f "$BIN"
echo "=== build ==="
if ! cmake --build "$BUILD" --target CFDSpeciesTests -j 20 2>&1 | tail -25; then
  echo "BUILD FAILED"
  exit 1
fi
if [ ! -x "$BIN" ]; then
  echo "BUILD PRODUCED NO BINARY -- refusing to report a result"
  exit 1
fi
echo ""
echo "binary sha256: $(sha256sum "$BIN" | awk '{print $1}')"
echo ""
echo "=== P18.1 focused tests ==="
"$BIN" --gtest_filter='SpeciesRegistryTest.*:MassFractionStateTest.*'
P18_1_RC=$?
echo ""
echo "=== the whole pre-existing species unit suite (preservation) ==="
"$BIN"
ALL_RC=$?
echo ""
echo "P18.1 focused rc=$P18_1_RC   full species unit suite rc=$ALL_RC"
exit $(( P18_1_RC | ALL_RC ))

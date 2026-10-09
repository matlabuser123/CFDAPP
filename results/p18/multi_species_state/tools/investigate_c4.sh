#!/usr/bin/env bash
# P18.1 -- investigate why C4 (identity) reported VACUOUS while one
# unpredicted test failed. Investigation only: the mutation is applied and
# restored under sha256 verification, exactly as in the control run.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1
BUILD=build/final
BIN=$BUILD/tests/unit/species/CFDSpeciesTests
F=src/species/SpeciesRegistry.cpp
BK=/tmp/p18_1_c4
rm -rf "$BK"; mkdir -p "$BK"
cp "$F" "$BK/orig"
sha256sum "$F" > "$BK/pre.sha256"

sed -i 's|return lhs.names() == rhs.names();|return lhs.names().size() == rhs.names().size();|' "$F"
echo "=== mutated operator== ==="
grep -A1 'bool operator==' "$F" | sed 's/^/  /'
echo ""
rm -f "$BIN"
cmake --build "$BUILD" --target CFDSpeciesTests -j 20 > /tmp/p18_1_c4_build.log 2>&1
if [ ! -x "$BIN" ]; then echo "BUILD FAILED"; grep error: /tmp/p18_1_c4_build.log | head -5; fi

echo "=== which tests actually fail under C4 ==="
"$BIN" --gtest_filter='SpeciesRegistryTest.*:MassFractionStateTest.*' > /tmp/p18_1_c4_run.log 2>&1
grep '^\[  FAILED  \].*\.' /tmp/p18_1_c4_run.log | sort -u | sed 's/^/  /'
echo ""
echo "=== the failure detail ==="
grep -B2 -A6 'Failure' /tmp/p18_1_c4_run.log | head -40 | sed 's/^/  /'
echo ""
echo "=== what LookupIsIndependentOfDeclarationOrder did ==="
"$BIN" --gtest_filter='SpeciesRegistryTest.LookupIsIndependentOfDeclarationOrder' 2>&1 \
  | tail -12 | sed 's/^/  /'

cp "$BK/orig" "$F"
sha256sum -c "$BK/pre.sha256" --quiet && echo "" && echo "RESTORED (sha256 verified)"
rm -f "$BIN"
cmake --build "$BUILD" --target CFDSpeciesTests -j 20 > /dev/null 2>&1

#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
B=./build/final/tests/unit/species/CFDSpeciesTests

echo "=== whole species unit suite ==="
"$B" 2>&1 | tail -8
echo ""
echo "=== per P18.2 suite ==="
for s in SpeciesClosureTest SpeciesFaceConservationTest SpeciesSourceTest \
         SpeciesBoundaryTest SpeciesTransientTest SpeciesTransportTest \
         SpeciesCanonicalTest SpeciesConservationTest SpeciesControlTest; do
  out=$("$B" --gtest_filter="${s}.*" 2>&1)
  total=$(echo "$out" | grep -cE '^\[ RUN      \]')
  passed=$(echo "$out" | grep -cE '^\[       OK \]')
  failed=$(( total - passed ))
  printf '  %-34s %2d run  %2d passed  %2d FAILED\n' "$s" "$total" "$passed" "$failed"
done
echo ""
echo "=== new P18.2 tests, counted ==="
NEW=$("$B" --gtest_list_tests --gtest_filter='SpeciesClosureTest.*:SpeciesFaceConservationTest.*:SpeciesSourceTest.*:SpeciesBoundaryTest.*:SpeciesTransientTest.*:SpeciesTransportTest.*:SpeciesCanonicalTest.*:SpeciesConservationTest.*:SpeciesControlTest.*' 2>/dev/null | grep -c '^  ')
echo "  P18.2 tests : $NEW"
echo "  P18.1 tests : $("$B" --gtest_list_tests --gtest_filter='SpeciesRegistryTest.*:MassFractionStateTest.*' 2>/dev/null | grep -c '^  ')"
echo "  pre-P18     : $("$B" --gtest_list_tests --gtest_filter='-SpeciesRegistryTest.*:MassFractionStateTest.*:SpeciesClosureTest.*:SpeciesFaceConservationTest.*:SpeciesSourceTest.*:SpeciesBoundaryTest.*:SpeciesTransientTest.*:SpeciesTransportTest.*:SpeciesCanonicalTest.*:SpeciesConservationTest.*:SpeciesControlTest.*' 2>/dev/null | grep -c '^  ')"
echo "  total       : $("$B" --gtest_list_tests 2>/dev/null | grep -c '^  ')"
echo ""
echo "=== the failing criterion, verbatim ==="
"$B" --gtest_filter='SpeciesConservationTest.ClosedEnclosureConservesEverySpeciesMass' 2>&1 \
  | grep -E 'C\+\+ exception|sum to'

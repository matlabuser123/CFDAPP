#!/usr/bin/env bash
# P18.1 section 37 -- the quantitative report. Counts and measurements, not
# PASS/FAIL. Everything here is read from the built binary and the probe, not
# transcribed by hand.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1
B=./build/final/tests/unit/species/CFDSpeciesTests

echo "=== test counts (from --gtest_list_tests, not counted by hand) ==="
REG=$("$B" --gtest_list_tests --gtest_filter='SpeciesRegistryTest.*' 2>/dev/null | grep -c '^  ')
MFS=$("$B" --gtest_list_tests --gtest_filter='MassFractionStateTest.*' 2>/dev/null | grep -c '^  ')
PRE=$("$B" --gtest_list_tests --gtest_filter='-SpeciesRegistryTest.*:MassFractionStateTest.*' 2>/dev/null | grep -c '^  ')
TOT=$("$B" --gtest_list_tests 2>/dev/null | grep -c '^  ')
echo "  SpeciesRegistryTest    (new P18.1) : $REG"
echo "  MassFractionStateTest  (new P18.1) : $MFS"
echo "  pre-existing species unit tests    : $PRE"
echo "  total in CFDSpeciesTests           : $TOT"
echo ""
echo "=== measurements ==="
./build/final/p18_1_measure

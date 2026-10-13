#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
B=./build/final/tests/unit/species/CFDSpeciesTests
echo "SpeciesRegistryTest   : $("$B" --gtest_list_tests --gtest_filter='SpeciesRegistryTest.*' 2>/dev/null | grep -c '^  ')"
echo "MassFractionStateTest : $("$B" --gtest_list_tests --gtest_filter='MassFractionStateTest.*' 2>/dev/null | grep -c '^  ')"
echo "NEW P18.1 TOTAL       : $("$B" --gtest_list_tests --gtest_filter='SpeciesRegistryTest.*:MassFractionStateTest.*' 2>/dev/null | grep -c '^  ')"
echo "pre-existing species  : $("$B" --gtest_list_tests --gtest_filter='-SpeciesRegistryTest.*:MassFractionStateTest.*' 2>/dev/null | grep -c '^  ')"
echo "binary total          : $("$B" --gtest_list_tests 2>/dev/null | grep -c '^  ')"

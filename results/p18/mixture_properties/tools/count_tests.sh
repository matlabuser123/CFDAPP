#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
B=./build/final/tests/unit/species/CFDSpeciesTests
echo "P18.3 MixtureThermodynamicsTest : $("$B" --gtest_list_tests --gtest_filter='MixtureThermodynamicsTest.*' 2>/dev/null | grep -c '^  ')"
echo "P18.3 SpeciesThermodynamicsTest : $("$B" --gtest_list_tests --gtest_filter='SpeciesThermodynamicsTest.*' 2>/dev/null | grep -c '^  ')"
echo "P18.3 total                     : $("$B" --gtest_list_tests --gtest_filter='MixtureThermodynamicsTest.*:SpeciesThermodynamicsTest.*' 2>/dev/null | grep -c '^  ')"
echo "CFDSpeciesTests total           : $("$B" --gtest_list_tests 2>/dev/null | grep -c '^  ')"

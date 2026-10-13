#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
B=./build/final/tests/unit/species/CFDSpeciesTests
F='ReactionTest.*:ReactionMechanismTest.*:ReactionSourceTest.*:ReactionTransportTest.*'
echo "P18.4 ReactionTest          : $("$B" --gtest_list_tests --gtest_filter='ReactionTest.*' 2>/dev/null | grep -c '^  ')"
echo "P18.4 ReactionMechanismTest : $("$B" --gtest_list_tests --gtest_filter='ReactionMechanismTest.*' 2>/dev/null | grep -c '^  ')"
echo "P18.4 ReactionSourceTest    : $("$B" --gtest_list_tests --gtest_filter='ReactionSourceTest.*' 2>/dev/null | grep -c '^  ')"
echo "P18.4 ReactionTransportTest : $("$B" --gtest_list_tests --gtest_filter='ReactionTransportTest.*' 2>/dev/null | grep -c '^  ')"
echo "P18.4 TOTAL                 : $("$B" --gtest_list_tests --gtest_filter="$F" 2>/dev/null | grep -c '^  ')"
echo "CFDSpeciesTests total       : $("$B" --gtest_list_tests 2>/dev/null | grep -c '^  ')"

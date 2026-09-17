#!/usr/bin/env bash
# P12-DIFF-002 A3-5: the two amended validations individually first, then the four focused suites.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/a3
L=$P/logs/05_A3_5_focused.log
cd $R
{
  echo "# P12-DIFF-002 A3-5 focused; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore $(sha256sum build/release/src/libcfdcore.a | cut -c1-16)"
  echo "# amended tests:"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
    tests/integration/case/test_multiblock_production_case.cpp | sed 's/^/  /'
  echo
  for t in CFDCaseIntegrationTests CFDDiscretizationTests CFDThermalTests CFDTurbulenceTests; do
    cmake --build build/release --target $t -j"$(nproc)" > /dev/null 2>&1
  done
  BIN=./build/release/tests/integration/case/CFDCaseIntegrationTests
  echo "===== amended test 1 ====="
  $BIN --gtest_filter='StructuredQuadProductionCase.NonOrthogonalCorrectionIsActiveOnTheProductionPath' 2>&1 | grep -E 'activation:|OK|FAILED|PASSED|tests ran'
  echo "===== amended test 2 ====="
  $BIN --gtest_filter='MultiBlockProductionCase.SectorConductionInterfaceConservation' 2>&1 | grep -E 'sector |imbalance|energy balance|observed order|OK|FAILED|PASSED|tests ran'
  echo
  for t in CFDCaseIntegrationTests CFDDiscretizationTests CFDThermalTests CFDTurbulenceTests; do
    B=$(find build/release -type f -name "$t" -perm -u+x | head -1)
    OUT=$("$B" 2>&1)
    echo "$t: $(echo "$OUT" | grep -E '^\[==========\] [0-9]+ tests from|^\[  PASSED  \]|^\[  FAILED  \]' | tr '\n' ' ')"
  done
} > $L 2>&1
cat $L

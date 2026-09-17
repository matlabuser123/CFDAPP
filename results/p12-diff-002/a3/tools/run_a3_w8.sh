#!/usr/bin/env bash
# P12-DIFF-002 A3-7: the two historical W8 tests, rerun UNCHANGED. Their full output is captured so
# every resolution's raw value and raw error is on record BEFORE any observed order / GCI is read.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/a3
L=$P/logs/07_A3_7_W8.log
cd $R
{
  echo "# P12-DIFF-002 A3-7 W8; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore $(sha256sum build/release/src/libcfdcore.a | cut -c1-16)"
  echo "# W8 test sources -- NOT amended by A3; A3 touched only the two tests it reconciled:"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
    tests/integration/case/test_multiblock_production_case.cpp | sed 's/^/  /'
  echo
  B=./build/release/tests/integration/case/CFDCaseIntegrationTests
  echo "===== StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence ====="
  $B --gtest_filter='StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence' 2>&1
  echo "  (exit $?)"
  echo
  echo "===== MultiBlockProductionCase.CurvedChannelGridConvergence ====="
  $B --gtest_filter='MultiBlockProductionCase.CurvedChannelGridConvergence' 2>&1
  echo "  (exit $?)"
} > $L 2>&1
grep -E 'x8:|x12:|x18:|pair |Failure|Expected|actual:|FAILED|PASSED|coarse|medium|fine|exit' $L | head -60

#!/usr/bin/env bash
# P12-DIFF-002 W6 + W8: the two historical production tests, rerun UNCHANGED. Their own printf
# output carries W6's quantities (iterations, velocity L2, dp/dx, observed orders, GCI report), so
# one run serves both criteria. No threshold, test or golden output is modified.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
L=$P/logs/14_W6_W8_historical.log
cd $R
{
  echo "# P12-DIFF-002 W6/W8 (libcfdcore.a sha256 $(sha256sum build/release/src/libcfdcore.a | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# the two historical tests, UNCHANGED:"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
    tests/integration/case/test_multiblock_production_case.cpp | sed 's/^/  /'
  echo
  cmake --build build/release --target CFDCaseIntegrationTests -j"$(nproc)" 2>&1 | tail -2
  echo
  echo "===== StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence ====="
  ./build/release/tests/integration/case/CFDCaseIntegrationTests \
    --gtest_filter='StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence' 2>&1
  echo "  (exit $?)"
  echo
  echo "===== MultiBlockProductionCase.CurvedChannelGridConvergence ====="
  ./build/release/tests/integration/case/CFDCaseIntegrationTests \
    --gtest_filter='MultiBlockProductionCase.CurvedChannelGridConvergence' 2>&1
  echo "  (exit $?)"
} > $L 2>&1
cat $L

#!/usr/bin/env bash
# P12-DIFF-002 A2-4: the k-epsilon regression test, rerun with its ORIGINAL threshold and
# configuration. The test source is unchanged (sha256 recorded below).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/a2
L=$P/logs/07_A2_4_kepsilon.log
cd $R
{
  echo "# P12-DIFF-002 A2-4 k-epsilon; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore $(sha256sum build/release/src/libcfdcore.a | cut -c1-16)"
  echo "# test source UNCHANGED:"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp | sed 's/^/  /'
  echo
  cmake --build build/release --target CFDCaseIntegrationTests -j"$(nproc)" 2>&1 | tail -2
  echo
  ./build/release/tests/integration/case/CFDCaseIntegrationTests \
    --gtest_filter='StructuredQuadProductionCase.KEpsilonChannelMatchesCartesianOnTiltedMesh' 2>&1
  echo "  (exit $?)"
} > $L 2>&1
cat $L

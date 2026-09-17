#!/usr/bin/env bash
# P12-DIFF-002 W8, run UNCHANGED at its own decision point.
#
# Frozen W8 (results/p12-diff-002/acceptance_gate.md):
#   "StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence and
#    MultiBlockProductionCase.CurvedChannelGridConvergence rerun unchanged.
#    Reported, not presupposed. ... If either still fails: STOP, preserve the result, and request
#    a separate decision on amending the historical gate. Do not retune it"
#
# Nothing is amended here. The COMPLETE raw output is captured so every quantity that feeds an
# observed order is recorded before any order is computed.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/w8
mkdir -p "$P/logs"
cd "$R" || exit 1
BIN=$(find build/release -type f -name CFDCaseIntegrationTests | head -1)
if [ -z "$BIN" ]; then echo "W8 NOT RUN -- binary not found"; exit 1; fi
chmod +x "$BIN"
{
  echo "# P12-DIFF-002 W8 -- original two tests, unchanged; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore.a  $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# test binary   $(sha256sum "$BIN" | cut -d' ' -f1)"
  echo "# test sources (unchanged by VAL-001):"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
            tests/integration/case/test_multiblock_production_case.cpp | sed 's/^/#   /'
  echo
  ./"$BIN" --gtest_filter='StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence:MultiBlockProductionCase.CurvedChannelGridConvergence'
  echo "exit $?"
} > "$P/logs/01_W8.log" 2>&1
grep -E "^\[ RUN|^\[  FAILED|^\[       OK|Failure|Expected|actual|pair|observed order|^ *\| |grid|exit " "$P/logs/01_W8.log" | head -80

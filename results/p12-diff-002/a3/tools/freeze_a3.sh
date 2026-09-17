#!/usr/bin/env bash
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
L=$P/a3/logs/03_gate_a3_freeze.log
cd $P
{
  echo "# P12-DIFF-002 A3 GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(cd $R && git rev-parse HEAD)"
  echo
  echo "# THE A3 GATE (frozen now):"
  sha256sum acceptance_gate_A3.md | sed 's/^/  /'
  echo
  echo "# the replacement instruments and the non-vacuous pre-freeze dry-run:"
  sha256sum a3/tools/diff2_a3_dryrun.cpp a3/tools/run_a3.sh a3/tools/hash_production.sh \
    a3/logs/01_production_before_A3.log a3/logs/02_dryrun.log | sed 's/^/  /'
  echo
  echo "# PRESERVED chronology - original, A1, A2:"
  sha256sum acceptance_gate.md acceptance_gate_A1.md acceptance_gate_A2.md summary.md \
    a1/summary_a1.md a2/summary_a2.md logs/08_gate_W3b.log logs/09_gate_W3b_patches.log \
    a1/logs/04_W3bA1_FRESH_production.log a2/logs/01_negative_control_preA2.log \
    a2/logs/10_A2_7_focused.log logs/05_cases_before.log | sed 's/^/  /'
  echo
  echo "# the two tests A3 will amend, BEFORE amendment:"
  (cd $R && sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
     tests/integration/case/test_multiblock_production_case.cpp | sed 's/^/  /')
} > $L 2>&1
cat $L

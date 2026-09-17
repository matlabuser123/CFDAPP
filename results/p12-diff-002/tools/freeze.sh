#!/usr/bin/env bash
# P12-DIFF-002: freeze the acceptance gate and architecture record, and hash the source/binary
# state, BEFORE any change to production numerical behaviour. The only preceding production edit is
# the numerically neutral DIFF-002-A interface widening (verified neutral in logs/03).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
L=$P/logs/06_gate_freeze.log
mkdir -p $P/logs $P/data
cd $P
{
  echo "# P12-DIFF-002 GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(cd $R && git rev-parse HEAD)"
  echo
  echo "# frozen documents, tools and the pre-freeze dry-run:"
  sha256sum acceptance_gate.md architecture.md tools/diff2_gate.cpp tools/diff2_cases.cpp \
    tools/run.sh logs/04_gate_dryrun_baseline.log logs/05_cases_before.log | sed 's/^/  /'
  echo
  echo "# production source at freeze time (interface widened, numerics unchanged):"
  (cd $R && sha256sum src/discretization/NonOrthogonalDiffusion.cpp \
     include/cfd/discretization/NonOrthogonalDiffusion.hpp src/physics/MomentumEquation.cpp \
     src/thermal/EnergyEquation.cpp include/cfd/thermal/EnergyEquation.hpp \
     src/species/SpeciesEquation.cpp src/turbulence/KEpsilonEquation.cpp \
     src/discretization/Diffusion.cpp src/discretization/Gradient.cpp | sed 's/^/  /')
  echo "# binary at freeze time:"
  (cd $R && sha256sum build/release/src/libcfdcore.a | sed 's/^/  /')
  echo
  echo "# preserved prior evidence (unchanged):"
  sha256sum investigation/plan.md investigation/summary.md | sed 's/^/  /'
  (cd $R/results/p12-diff-001 && sha256sum acceptance_gate.md summary.md | sed 's/^/  /')
  (cd $R/results/p12-grad-002 && sha256sum acceptance_gate.md acceptance_gate_A1.md summary.md \
     a1/summary_a1.md investigation/summary.md | sed 's/^/  /')
  (cd $R/results/p12-mesh-007 && sha256sum acceptance_gate.md summary.md | sed 's/^/  /')
  echo "# the two historical tests and their cases (unchanged):"
  (cd $R && sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
     tests/integration/case/test_multiblock_production_case.cpp | sed 's/^/  /')
} > $L 2>&1
cat $L

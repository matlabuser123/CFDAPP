#!/usr/bin/env bash
# P12-DIFF-002 Amendment A1: freeze and hash acceptance_gate_A1.md, the oracle probe and the
# two-sided pre-freeze dry-run, BEFORE the fresh W3b-A1 acceptance run. No production source is
# modified by A1 -- the hashes below record that.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
L=$P/a1/logs/03_gate_a1_freeze.log
mkdir -p $P/a1/logs $P/a1/data
cd $P
{
  echo "# P12-DIFF-002 A1 GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(cd $R && git rev-parse HEAD)"
  echo
  echo "# THE A1 GATE (frozen now):"
  sha256sum acceptance_gate_A1.md | sed 's/^/  /'
  echo
  echo "# the independent oracle and the two-sided pre-freeze dry-run:"
  sha256sum a1/tools/diff2_oracle.cpp a1/tools/run_a1.sh a1/logs/01_oracle_dryrun.log \
    a1/logs/02_dryrun_production.log a1/logs/02_dryrun_baseline.log \
    a1/logs/02_dryrun_overeager.log | sed 's/^/  /'
  echo
  echo "# THE ORIGINAL GATE AND ITS W3b FAILURE -- preserved, not deleted, not rewritten:"
  sha256sum acceptance_gate.md summary.md logs/08_gate_W3b.log logs/09_gate_W3b_patches.log \
    logs/10_gate_W4.log logs/04_gate_dryrun_baseline.log logs/05_cases_before.log \
    logs/07_gate_W1_W2_W3.log | sed 's/^/  /'
  echo
  echo "# production source at A1 freeze time (UNCHANGED by A1):"
  (cd $R && sha256sum src/discretization/NonOrthogonalDiffusion.cpp \
     include/cfd/discretization/NonOrthogonalDiffusion.hpp src/mesh/MeshGeometry.cpp \
     include/cfd/mesh/MeshGeometry.hpp src/physics/MomentumEquation.cpp \
     src/thermal/EnergyEquation.cpp include/cfd/thermal/EnergyEquation.hpp \
     src/species/SpeciesEquation.cpp src/turbulence/KEpsilonEquation.cpp \
     src/discretization/Diffusion.cpp src/discretization/Gradient.cpp | sed 's/^/  /')
  echo "# binary at A1 freeze time:"
  (cd $R && sha256sum build/release/src/libcfdcore.a | sed 's/^/  /')
  echo
  echo "# the two historical tests and the W4 tests (unchanged):"
  (cd $R && sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
     tests/integration/case/test_multiblock_production_case.cpp \
     tests/unit/discretization/test_boundary_reconstruction.cpp | sed 's/^/  /')
} > $L 2>&1
cat $L

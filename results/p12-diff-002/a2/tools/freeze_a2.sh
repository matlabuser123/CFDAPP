#!/usr/bin/env bash
# P12-DIFF-002 Amendment A2: freeze and hash acceptance_gate_A2.md, the audit and the non-vacuous
# pre-freeze negative control, BEFORE changing production behaviour.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
L=$P/a2/logs/02_gate_a2_freeze.log
mkdir -p $P/a2/logs $P/a2/data
cd $P
{
  echo "# P12-DIFF-002 A2 GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(cd $R && git rev-parse HEAD)"
  echo
  echo "# THE A2 GATE (frozen now):"
  sha256sum acceptance_gate_A2.md | sed 's/^/  /'
  echo
  echo "# the audit, the instrument and the non-vacuous pre-freeze negative control:"
  sha256sum a2/activation_architecture.md a2/tools/diff2_activation.cpp a2/tools/run_a2.sh \
    a2/logs/01_negative_control_preA2.log | sed 's/^/  /'
  echo
  echo "# PRESERVED -- original gate, A1 gate, original W3b failure, A1 PASS, W7 failure:"
  sha256sum acceptance_gate.md acceptance_gate_A1.md summary.md a1/summary_a1.md \
    logs/08_gate_W3b.log logs/09_gate_W3b_patches.log a1/logs/04_W3bA1_FRESH_production.log \
    a1/logs/03_gate_a1_freeze.log logs/15_W7_focused.log logs/14_W6_W8_historical.log \
    logs/04_gate_dryrun_baseline.log logs/05_cases_before.log | sed 's/^/  /'
  echo
  echo "# production source at A2 freeze time (BEFORE the activation change):"
  (cd $R && sha256sum src/discretization/NonOrthogonalDiffusion.cpp \
     include/cfd/discretization/NonOrthogonalDiffusion.hpp src/mesh/MeshGeometry.cpp \
     include/cfd/mesh/MeshGeometry.hpp src/physics/MomentumEquation.cpp \
     include/cfd/physics/MomentumEquation.hpp src/thermal/EnergyEquation.cpp \
     include/cfd/thermal/EnergyEquation.hpp src/species/SpeciesEquation.cpp \
     src/turbulence/KEpsilonEquation.cpp src/pressure_velocity/SIMPLESettings.cpp \
     src/discretization/Diffusion.cpp src/discretization/Gradient.cpp | sed 's/^/  /')
  echo "# binary at A2 freeze time:"
  (cd $R && sha256sum build/release/src/libcfdcore.a | sed 's/^/  /')
  echo
  echo "# the historical tests and the W4 tests (unchanged, and must stay so):"
  (cd $R && sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
     tests/integration/case/test_multiblock_production_case.cpp \
     tests/unit/discretization/test_boundary_reconstruction.cpp | sed 's/^/  /')
} > $L 2>&1
cat $L

#!/usr/bin/env bash
# P12-DIFF-002-W8-INV-001 step 1: freeze the investigation baseline. READ-ONLY.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/w8-inv-001
mkdir -p "$P/logs" "$P/data"
cd "$R" || exit 1
{
  echo "# P12-DIFF-002-W8-INV-001 frozen baseline; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo
  echo "## the two W8 test sources"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
            tests/integration/case/test_multiblock_production_case.cpp \
            tests/integration/case/CMakeLists.txt
  echo
  echo "## mesh generators"
  sha256sum src/mesh/MeshGeometry.cpp include/cfd/mesh/MeshGeometry.hpp \
            src/io/case/MeshConfigParser.cpp include/cfd/io/case/MeshConfig.hpp
  echo
  echo "## extraction / validation utilities"
  sha256sum src/validation/GridConvergence.cpp include/cfd/validation/GridConvergence.hpp \
            src/validation/GridConvergenceStudy.cpp include/cfd/validation/GridConvergenceStudy.hpp \
            src/validation/ProductionValidation.cpp include/cfd/validation/ProductionValidation.hpp \
            src/validation/ErrorNorms.cpp include/cfd/validation/ErrorNorms.hpp 2>/dev/null
  echo
  echo "## production numerical files relevant to these two cases"
  sha256sum src/discretization/NonOrthogonalDiffusion.cpp src/discretization/Gradient.cpp \
            src/discretization/Diffusion.cpp src/physics/MomentumEquation.cpp \
            src/pressure_velocity/SIMPLE.cpp src/pressure_velocity/PressureCorrectionEquation.cpp \
            src/solver/SolverRobustness.cpp
  echo
  echo "## library"
  sha256sum build/release/src/libcfdcore.a
  echo
  echo "## the original W8 gate (DIFF-002 acceptance_gate.md) and the DIFF-002 lineage"
  sha256sum results/p12-diff-002/acceptance_gate.md \
            results/p12-diff-002/acceptance_gate_A1.md \
            results/p12-diff-002/acceptance_gate_A2.md \
            results/p12-diff-002/acceptance_gate_A3.md \
            results/p12-diff-002/validation-migration/acceptance_gate.md \
            results/p12-diff-002/thermal-interface-fix/acceptance_gate.md \
            results/p12-diff-002/validation-migration/acceptance_gate_A5.md \
            results/p12-diff-002/rob-001/acceptance_gate.md \
            results/p12-diff-002/validation-migration/acceptance_gate_A6.md \
            results/p12-diff-002/uc-001/acceptance_gate.md \
            results/p12-diff-002/uf-001/acceptance_gate.md \
            results/p12-grad-002/val-001/acceptance_gate.md
  echo
  echo "## MESH-001 evidence (no acceptance_gate.md file in that directory)"
  sha256sum results/p12-mesh-001/summary.md
  echo
  echo "## MESH-003 gate and evidence"
  sha256sum results/p12-mesh-003/acceptance_gate.md results/p12-mesh-003/summary.md
  echo
  echo "## the preserved W8 failure, verbatim"
  sha256sum results/p12-diff-002/w8/logs/01_W8.log results/p12-diff-002/w8/summary.md
} > "$P/logs/00_freeze.log" 2>&1
cat "$P/logs/00_freeze.log"

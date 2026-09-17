#!/usr/bin/env bash
# P12-DIFF-002-UF-001.1: freeze the investigation baseline. Read-only.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uf-001
mkdir -p "$P/logs" "$P/data"
cd "$R" || exit 1
{
  echo "# P12-DIFF-002-UF-001.1 frozen baseline; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  echo "## natural-convection validation tests + benchmark data + extrema extraction"
  sha256sum tests/integration/thermal/test_natural_convection_validation.cpp \
            tests/integration/thermal/NaturalConvectionValidationUtils.hpp \
            tests/integration/thermal/NaturalConvectionValidationUtils.cpp \
            tests/integration/thermal/DeVahlDavis1983.hpp \
            tests/integration/thermal/CMakeLists.txt
  echo
  echo "## literature provenance copy (must stay numerically in sync)"
  sha256sum validation/literature/natural_convection/* 2>/dev/null
  echo
  echo "## momentum assembly"
  sha256sum src/physics/MomentumEquation.cpp include/cfd/physics/MomentumEquation.hpp
  echo
  echo "## thermal assembly"
  sha256sum src/thermal/EnergyEquation.cpp include/cfd/thermal/EnergyEquation.hpp \
            src/thermal/ThermalSolver.cpp src/thermal/ThermalInterface.cpp
  echo
  echo "## buoyancy / source"
  sha256sum src/physics/BoussinesqBuoyancy.cpp include/cfd/physics/BoussinesqBuoyancy.hpp
  echo
  echo "## boundary diffusion (the DIFF-002 reconstruction)"
  sha256sum src/discretization/NonOrthogonalDiffusion.cpp \
            include/cfd/discretization/NonOrthogonalDiffusion.hpp \
            src/discretization/Diffusion.cpp
  echo
  echo "## pressure-velocity coupling"
  sha256sum src/pressure_velocity/SIMPLE.cpp
  echo
  echo "## library"
  sha256sum build/release/src/libcfdcore.a
  echo
  echo "## all frozen gates (must stay byte-identical through UF-001)"
  sha256sum results/p12-diff-002/acceptance_gate.md \
            results/p12-diff-002/acceptance_gate_A1.md \
            results/p12-diff-002/acceptance_gate_A2.md \
            results/p12-diff-002/acceptance_gate_A3.md \
            results/p12-diff-002/validation-migration/acceptance_gate.md \
            results/p12-diff-002/thermal-interface-fix/acceptance_gate.md \
            results/p12-diff-002/validation-migration/acceptance_gate_A5.md \
            results/p12-diff-002/rob-001/acceptance_gate.md \
            results/p12-diff-002/validation-migration/acceptance_gate_A6.md \
            results/p12-diff-002/uc-001/acceptance_gate.md
  echo
  echo "## UC-001 deliverables (must stay byte-identical)"
  sha256sum results/p12-diff-002/uc-001/plan.md results/p12-diff-002/uc-001/summary.md \
            tests/integration/poiseuille/PoiseuilleValidationUtils.hpp \
            tests/integration/poiseuille/PoiseuilleValidationUtils.cpp \
            tests/integration/poiseuille/test_poiseuille_production_validation.cpp
  echo
  echo "## pre-DIFF-002 baseline reference used for comparison"
  echo "The pre-DIFF-002 library is produced by the reconstruct-and-prove method recorded in"
  echo "results/p12-diff-002/validation-migration/a6/resumed (A6-f) and rob-001: an isolated copy"
  echo "of the tree outside the repo with the DIFF-002 far-cell reconstruction removed, whose"
  echo "rebuilt libcfdcore.a hash is checked against the value frozen when the change was made."
  echo "A5 recorded that library as sha256 90473bfb... (reconstruction removed)."
} > "$P/logs/00_freeze.log" 2>&1
cat "$P/logs/00_freeze.log"

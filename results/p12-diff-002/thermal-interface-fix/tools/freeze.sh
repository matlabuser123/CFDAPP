#!/usr/bin/env bash
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/thermal-interface-fix
mkdir -p $P/logs
cd $R
{
  echo "# ThermalInterface fix GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo
  echo "## THE GATE (frozen now, before any production modification):"
  sha256sum results/p12-diff-002/thermal-interface-fix/acceptance_gate.md | sed 's/^/  /'
  echo
  echo "## production numerical files BEFORE the fix:"
  sha256sum src/thermal/ThermalInterface.cpp src/thermal/EnergyEquation.cpp \
            include/cfd/thermal/EnergyEquation.hpp include/cfd/thermal/ThermalInterface.hpp \
            src/thermal/ThermalSolver.cpp src/physics/MomentumEquation.cpp \
            src/species/SpeciesEquation.cpp src/turbulence/KEpsilonEquation.cpp \
            src/discretization/NonOrthogonalDiffusion.cpp src/mesh/MeshGeometry.cpp \
            src/discretization/Gradient.cpp src/discretization/Diffusion.cpp | sed 's/^/  /'
  echo "## library BEFORE the fix:"
  sha256sum build/release/src/libcfdcore.a | sed 's/^/  /'
  echo
  echo "## preserved F-A evidence (unchanged):"
  sha256sum results/p12-diff-002/validation-migration/summary.md \
            results/p12-diff-002/validation-migration/acceptance_gate.md \
            results/p12-diff-002/investigation-f/logs/12_conjugate_divergence.log \
            results/p12-diff-002/investigation-f/summary.md | sed 's/^/  /'
  echo
  echo "## the test that detects the defect (must not be weakened):"
  sha256sum tests/unit/thermal/test_thermal_boundary_consistency.cpp | sed 's/^/  /'
} > $P/logs/00_freeze.log 2>&1
cat $P/logs/00_freeze.log

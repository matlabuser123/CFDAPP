#!/usr/bin/env bash
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/validation-migration
mkdir -p $P/logs
cd $R
{
  echo "# P12-DIFF-002 validation-migration GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo
  echo "## THE GATE (frozen now, before any test change):"
  sha256sum results/p12-diff-002/validation-migration/acceptance_gate.md | sed 's/^/  /'
  echo
  echo "## production numerical files (must not change):"
  sha256sum src/physics/MomentumEquation.cpp src/thermal/EnergyEquation.cpp \
            src/species/SpeciesEquation.cpp src/turbulence/KEpsilonEquation.cpp \
            src/discretization/NonOrthogonalDiffusion.cpp src/mesh/MeshGeometry.cpp \
            src/discretization/Gradient.cpp src/discretization/Diffusion.cpp \
            build/release/src/libcfdcore.a | sed 's/^/  /'
  echo
  echo "## the test files to be migrated, BEFORE migration:"
  sha256sum tests/unit/discretization/test_boundary_reconstruction.cpp \
            tests/unit/physics/test_momentum_diffusion.cpp \
            tests/unit/physics/test_momentum_variable_viscosity.cpp \
            tests/unit/thermal/test_energy_equation.cpp \
            tests/unit/thermal/test_energy_equation_variable_properties.cpp \
            tests/unit/thermal/test_thermal_region.cpp \
            tests/unit/thermal/test_thermal_boundary_consistency.cpp \
            tests/unit/thermal/test_sparse_assembly3d.cpp \
            tests/unit/species/test_species_equation.cpp \
            tests/integration/species/test_species_conservation.cpp \
            tests/solver/simple/test_simple_robustness.cpp | sed 's/^/  /'
  echo
  echo "## the UNCHANGED (U) test files -- must be byte-identical afterwards:"
  sha256sum tests/integration/poiseuille/test_poiseuille_production_validation.cpp \
            tests/integration/mms/test_mms_simple.cpp \
            tests/integration/thermal/test_natural_convection_validation.cpp \
            tests/integration/compressible/test_low_mach_regression.cpp \
            tests/unit/discretization/test_grid_refinement.cpp | sed 's/^/  /'
} > $P/logs/00_freeze.log 2>&1
cat $P/logs/00_freeze.log

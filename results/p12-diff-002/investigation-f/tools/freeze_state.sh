#!/usr/bin/env bash
# P12-DIFF-002-INV-002 (INV-F0): freeze investigation state. No acceptance gate -- this records what
# the investigation ran against so any later drift is detectable. Usage: freeze_state.sh <out>
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $R
{
  echo "# P12-DIFF-002-INV-002 state freeze; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo
  echo "## DIFF-002 production numerical files"
  sha256sum src/physics/MomentumEquation.cpp include/cfd/physics/MomentumEquation.hpp \
            src/thermal/EnergyEquation.cpp include/cfd/thermal/EnergyEquation.hpp \
            src/thermal/ThermalInterface.cpp src/thermal/ThermalSolver.cpp \
            src/species/SpeciesEquation.cpp include/cfd/species/SpeciesEquation.hpp \
            src/turbulence/KEpsilonEquation.cpp include/cfd/turbulence/KEpsilonEquation.hpp \
            src/discretization/NonOrthogonalDiffusion.cpp \
            include/cfd/discretization/NonOrthogonalDiffusion.hpp \
            src/discretization/Diffusion.cpp include/cfd/discretization/Diffusion.hpp \
            src/discretization/Gradient.cpp include/cfd/discretization/Gradient.hpp \
            src/discretization/VectorGradient.cpp src/discretization/Interpolation.cpp \
            src/mesh/MeshGeometry.cpp include/cfd/mesh/MeshGeometry.hpp \
            src/pressure_velocity/SIMPLESettings.cpp src/pressure_velocity/SIMPLE.cpp
  echo
  echo "## libcfdcore.a"
  sha256sum build/release/src/libcfdcore.a
  echo
  echo "## the four sub-class F tests"
  sha256sum tests/integration/species/test_species_conservation.cpp \
            tests/integration/thermal/test_natural_convection_validation.cpp
  find tests -name '*low_mach*' -o -name '*lowmach*' | while read -r f; do sha256sum "$f"; done
  echo
  echo "## their case / config inputs"
  for c in cases/species_diffusion cases/heated_cavity; do
    [ -d "$c" ] && sha256sum $c/*.json 2>/dev/null
  done
} > "$1" 2>&1
cat "$1"

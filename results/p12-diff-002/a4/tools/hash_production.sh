#!/usr/bin/env bash
# P12-DIFF-002 A4-0: hash every production numerical file and the linked numerical library.
# A4 is a validation/test migration phase; these must stay byte-identical throughout.
# Usage: hash_production.sh <output-file>
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $R
{
  echo "# P12-DIFF-002 A4 production freeze; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo "# production numerical source:"
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
  echo "# linked numerical library:"
  sha256sum build/release/src/libcfdcore.a
} > "$1" 2>&1
cat "$1"

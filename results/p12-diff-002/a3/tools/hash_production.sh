#!/usr/bin/env bash
# P12-DIFF-002 A3: hash the production numerical files. A3 is a validation/test amendment only, so
# these must be byte-identical before and after. Usage: hash_production.sh <log-name>
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/a3
mkdir -p $P/logs $P/data
cd $R
{
  echo "# P12-DIFF-002 A3 production hash; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  sha256sum src/physics/MomentumEquation.cpp \
            src/thermal/EnergyEquation.cpp \
            src/species/SpeciesEquation.cpp \
            src/turbulence/KEpsilonEquation.cpp \
            src/discretization/NonOrthogonalDiffusion.cpp \
            include/cfd/discretization/NonOrthogonalDiffusion.hpp \
            src/mesh/MeshGeometry.cpp \
            include/cfd/mesh/MeshGeometry.hpp \
            src/discretization/Gradient.cpp \
            src/discretization/Diffusion.cpp \
            include/cfd/physics/MomentumEquation.hpp \
            include/cfd/thermal/EnergyEquation.hpp \
            src/pressure_velocity/SIMPLESettings.cpp \
            src/thermal/ThermalInterface.cpp
  echo "# library:"
  sha256sum build/release/src/libcfdcore.a
} > $P/logs/$1 2>&1
cat $P/logs/$1

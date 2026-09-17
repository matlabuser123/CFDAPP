#!/usr/bin/env bash
# W8 amendment: freeze the gate and every file it references. Usage: freeze.sh <log-name>
R=/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$R" || exit 1
FILES=(
  results/p12-diff-002/w8a/acceptance_gate.md
  tests/integration/case/test_structured_quad_production_case.cpp
  tests/integration/case/test_multiblock_production_case.cpp
  results/p12-mesh-001/summary.md
  results/p12-mesh-003/acceptance_gate.md
  results/p12-mesh-003/summary.md
  results/p12-diff-002/w8-inv-001/summary.md
  results/p12-diff-002/w8/summary.md
  results/p12-diff-002/w8/logs/01_W8.log
  build/release/src/libcfdcore.a
  src/discretization/NonOrthogonalDiffusion.cpp
  src/discretization/Gradient.cpp
  src/physics/MomentumEquation.cpp
  src/pressure_velocity/SIMPLE.cpp
  src/solver/SolverRobustness.cpp
  src/mesh/MeshGeometry.cpp
  src/thermal/EnergyEquation.cpp
  src/thermal/ThermalInterface.cpp
)
missing=0
for f in "${FILES[@]}"; do [ -f "$f" ] || { echo "MISSING $f"; missing=1; }; done
[ "$missing" = 0 ] || exit 1
sha256sum "${FILES[@]}"

#!/usr/bin/env bash
# P12-GRAD-002-VAL-001 step 2: freeze the investigation baseline. Read-only.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/val-001
mkdir -p "$P/logs" "$P/data"
cd "$R" || exit 1
{
  echo "# P12-GRAD-002-VAL-001 frozen baseline; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo
  echo "## the single test instrument under investigation (and its support)"
  sha256sum tests/unit/discretization/test_grid_refinement.cpp \
            tests/unit/discretization/DistortedMesh.hpp \
            tests/unit/discretization/CMakeLists.txt
  echo
  echo "## gradient / boundary production files"
  sha256sum src/discretization/Gradient.cpp include/cfd/discretization/Gradient.hpp \
            src/discretization/VectorGradient.cpp include/cfd/discretization/VectorGradient.hpp \
            src/discretization/Diffusion.cpp include/cfd/discretization/Diffusion.hpp \
            src/discretization/NonOrthogonalDiffusion.cpp \
            include/cfd/discretization/NonOrthogonalDiffusion.hpp \
            src/mesh/MeshGeometry.cpp include/cfd/mesh/MeshGeometry.hpp
  echo
  echo "## library"
  sha256sum build/release/src/libcfdcore.a
  echo
  echo "## ALL GRAD-001 / GRAD-002 gates (must stay byte-identical)"
  sha256sum results/p12-grad-001/acceptance_gate.md \
            results/p12-grad-002/acceptance_gate.md \
            results/p12-grad-002/acceptance_gate_A1.md \
            results/p12-grad-002/formulation.md
  echo
  echo "## ALL DIFF-002 gates (must stay byte-identical)"
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
            results/p12-diff-002/uf-001/acceptance_gate.md
  echo
  echo "## MESH-007 gate (must not be touched)"
  sha256sum results/p12-mesh-007/acceptance_gate.md
  echo
  echo "## the preserved failure, verbatim (final-w7/logs/02_greengauss.log)"
  sha256sum results/p12-diff-002/final-w7/logs/02_greengauss.log
  grep -E "^(Grid|8x8|16x16|32x32|64x64)|Expected:|observed order" \
       results/p12-diff-002/final-w7/logs/02_greengauss.log | sed 's/^/  /'
} > "$P/logs/00_freeze.log" 2>&1
cat "$P/logs/00_freeze.log"

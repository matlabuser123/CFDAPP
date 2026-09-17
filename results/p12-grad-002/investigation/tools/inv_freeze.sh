#!/usr/bin/env bash
# P12-GRAD-002-INV-001: record the frozen state of the investigation and confirm that all prior
# evidence is unchanged. Investigation only -- nothing is modified.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/investigation
L=$P/logs/00_inv_freeze.log
mkdir -p $P/logs $P/data
cd $R
{
  echo "# P12-GRAD-002-INV-001 FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo "# working tree: $(git status --short | wc -l) entries ($(git status --short | grep -c '^ M') modified, $(git status --short | grep -c '^??') untracked)"
  echo
  echo "# libraries under comparison:"
  sha256sum build/release/src/libcfdcore.a $HOME/m7ref/base/build/src/libcfdcore.a \
    $HOME/m7ref/grad001/libcfdcore.a | sed 's/^/  /'
  echo "# production source under test (unchanged by this investigation):"
  sha256sum src/discretization/Gradient.cpp src/mesh/MeshGeometry.cpp \
    include/cfd/mesh/MeshGeometry.hpp | sed 's/^/  /'
  echo
  echo "# prior evidence, unchanged:"
  (cd results/p12-grad-002 && sha256sum acceptance_gate.md acceptance_gate_A1.md formulation.md \
     summary.md a1/audit.md a1/dryrun.md a1/summary_a1.md | sed 's/^/  /')
  (cd results/p12-grad-001 && sha256sum acceptance_gate.md summary.md | sed 's/^/  /')
  (cd results/p12-mesh-007 && sha256sum acceptance_gate.md summary.md | sed 's/^/  /')
  echo "# the two failing tests, unchanged:"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
    tests/integration/case/test_multiblock_production_case.cpp \
    tests/unit/discretization/test_grid_refinement.cpp | sed 's/^/  /'
  echo "# the two cases, unchanged:"
  sha256sum cases/poiseuille_distorted/*.json cases/curved_channel_multiblock/*.json | sed 's/^/  /'
} > $L 2>&1
cat $L

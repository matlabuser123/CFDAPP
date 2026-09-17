#!/usr/bin/env bash
# P12-GRAD-002 A2 freeze: sha256 of the gate, the dry-run record, every instrument, the candidate
# test file and the files the fresh execution is authorized to edit, plus the libraries.
# Refuses to run while any build or ctest of the repository is in progress.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002
LOG=$P/a2/logs/00_freeze.log
cd $R || exit 1
if pgrep -x ctest > /dev/null || pgrep -f "cmake --build" > /dev/null; then
  echo "REFUSED: a build or ctest is running"; pgrep -af "ctest|cmake --build"; exit 1
fi
[ -e "$LOG" ] && { echo "REFUSED: $LOG exists -- A2 is already frozen"; exit 1; }
{
  echo "# P12-GRAD-002 A2 freeze $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
  echo "## gates and records"
  sha256sum results/p12-grad-002/acceptance_gate.md results/p12-grad-002/acceptance_gate_A1.md \
            results/p12-grad-002/acceptance_gate_A2.md results/p12-grad-002/a2/dryrun.md \
            results/p12-grad-002/summary.md results/p12-grad-002/a1/summary_a1.md \
            results/p12-grad-002/val-001/acceptance_gate.md results/p12-grad-002/drift-001/acceptance_gate.md \
            results/p12-diff-002/w8b/acceptance_gate.md results/p12-mesh-007/acceptance_gate.md
  echo "## instruments"
  sha256sum results/p12-grad-002/a2/tools/*
  echo "## candidate test file and the files the fresh execution may edit"
  sha256sum tests/unit/discretization/test_gradient_boundary_consistency.cpp tests/unit/discretization/CMakeLists.txt
  echo "## production (must stay byte-identical)"
  echo "src/ + include/ tree: $(find src include -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
  sha256sum src/discretization/Gradient.cpp src/mesh/MeshGeometry.cpp include/cfd/discretization/Gradient.hpp
  echo "## libraries"
  sha256sum build/release/src/libcfdcore.a $HOME/g2/cur/build/src/libcfdcore.a $HOME/g2/nograd/build/src/libcfdcore.a \
            /root/m7ref/base/build/src/libcfdcore.a /root/m7ref/grad001/libcfdcore.a
  echo "## dry-run logs"
  sha256sum results/p12-grad-002/a2/logs/dry_* results/p12-grad-002/a2/logs/suite_* results/p12-grad-002/a2/logs/prod_*
} > $LOG 2>&1
chmod a-w $LOG
cat $LOG | head -40

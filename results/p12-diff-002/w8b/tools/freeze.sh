#!/usr/bin/env bash
# P12-DIFF-002 W8B + P12-GRAD-002-DRIFT-001 fix: freeze BEFORE any repository test or case edit.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$R" || exit 1
OUT=results/p12-diff-002/w8b/logs/00_freeze.log
{
  echo "# P12-DIFF-002 W8B / DRIFT-001 fix -- freeze, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo
  echo "## gates (frozen now)"
  sha256sum results/p12-diff-002/w8b/acceptance_gate.md results/p12-grad-002/drift-001/acceptance_gate.md
  echo
  echo "## candidate test files (the repository files must equal these after the edit)"
  sha256sum results/p12-diff-002/w8b/data/candidate/test_structured_quad_production_case.cpp \
            results/p12-diff-002/w8b/data/candidate/test_multiblock_production_case.cpp
  echo
  echo "## repository files BEFORE the edit"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
            tests/integration/case/test_multiblock_production_case.cpp \
            cases/poiseuille_distorted/solver.json cases/curved_channel_multiblock/solver.json \
            cases/poiseuille_distorted/case.json cases/curved_channel_multiblock/case.json
  echo "cases/ tree (all files except the four above): $(find cases -type f ! -path 'cases/poiseuille_distorted/solver.json' ! -path 'cases/curved_channel_multiblock/solver.json' ! -path 'cases/poiseuille_distorted/case.json' ! -path 'cases/curved_channel_multiblock/case.json' ! -path '*/results/*' -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
  echo
  echo "## production (must not change)"
  sha256sum src/discretization/NonOrthogonalDiffusion.cpp src/discretization/Gradient.cpp \
            src/physics/MomentumEquation.cpp src/pressure_velocity/SIMPLE.cpp \
            src/solver/SolverRobustness.cpp src/mesh/MeshGeometry.cpp \
            src/thermal/EnergyEquation.cpp src/thermal/ThermalInterface.cpp
  echo "src/ + include/ tree: $(find src include -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
  echo "build/release/src/libcfdcore.a: $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo
  echo "## evidence that must stay byte-identical"
  sha256sum results/p12-diff-002/w8/summary.md results/p12-diff-002/w8/logs/01_W8.log \
            results/p12-diff-002/w8-inv-001/summary.md results/p12-diff-002/w8a/acceptance_gate.md \
            results/p12-diff-002/w8a/summary.md results/p12-diff-002/uc-001/acceptance_gate.md \
            results/p12-mesh-001/summary.md results/p12-mesh-003/acceptance_gate.md \
            results/p12-mesh-003/summary.md results/p12-diff-002/acceptance_gate.md \
            results/p12-grad-002/drift-001/summary.md
  echo
  echo "## pre-freeze dry-run logs"
  sha256sum results/p12-diff-002/w8b/logs/dry_*.log results/p12-grad-002/drift-001/logs/d1_dryrun_rc_cases.log
  echo "exit 0"
} > "$OUT" 2>&1
cat "$OUT"

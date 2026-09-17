#!/usr/bin/env bash
# P12-GRAD-002 Amendment A1: freeze A1 after the pre-freeze baseline dry-run and before any fresh
# acceptance run, and record the final formatted-source binary hash (authorization item 8).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002
L=$P/a1/logs/01_a1_freeze.log
cd $P
{
  echo "# P12-GRAD-002 A1 FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(cd $R && git rev-parse HEAD)"
  echo
  echo "# A1 documents, tools and the pre-freeze dry-run:"
  sha256sum acceptance_gate_A1.md a1/audit.md a1/dryrun.md a1/logs/00_dryrun_baseline.log \
    tools/a1_envelope.cpp tools/diagnostics.cpp tools/run_a1.sh tools/a1_freeze.sh | sed 's/^/  /'
  echo
  echo "# the ORIGINAL gate, its failure and GRAD-001 remain unchanged:"
  sha256sum acceptance_gate.md formulation.md summary.md logs/00_gate_freeze.log \
    logs/07_sweep_corrected_new.log logs/08_sweep_corrected_base.log | sed 's/^/  /'
  (cd $R/results/p12-grad-001 && sha256sum acceptance_gate.md summary.md | sed 's/^/  /')
  echo "# MESH-007's frozen gate and its original failed G6.3 evidence remain unchanged:"
  (cd $R/results/p12-mesh-007 && sha256sum acceptance_gate.md \
     logs/12_gate_stage3_G1.2_G5_G6_G7_G8.log \
     logs/15_diag_g63_static_translation_BASE_and_NEW.log | sed 's/^/  /')
  echo
  echo "# production source under test (unchanged by A1; formatted, clang-format clean):"
  (cd $R && sha256sum src/discretization/Gradient.cpp src/mesh/MeshGeometry.cpp \
     include/cfd/mesh/MeshGeometry.hpp | sed 's/^/  /')
  echo "# rebuild of the final formatted source, and its frozen binary hash:"
  (cd $R && cmake --build build/release --target cfdcore -j"$(nproc)" 2>&1 | tail -2 | sed 's/^/  /')
  (cd $R && sha256sum build/release/src/libcfdcore.a | sed 's/^/  /')
  echo "# reference libraries used by the dry-run and the negative controls:"
  sha256sum $HOME/m7ref/base/build/src/libcfdcore.a $HOME/m7ref/grad001/libcfdcore.a | sed 's/^/  /'
} > $L 2>&1
cat $L

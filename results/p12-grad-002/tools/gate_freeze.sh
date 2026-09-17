#!/usr/bin/env bash
# P12-GRAD-002: freeze the gate and the derivation BEFORE any production source change,
# and snapshot the pre-GRAD-002 library so every comparison stays reproducible afterwards.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002
L=$P/logs/00_gate_freeze.log
S=$HOME/m7ref/grad001
mkdir -p $P/logs $P/data $S
rm -rf $S/include
cp -r $R/include $S/include
cp $R/build/release/src/libcfdcore.a $S/libcfdcore.a
cd $P
{
  echo "# P12-GRAD-002 GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(cd $R && git rev-parse HEAD)"
  echo
  echo "# frozen documents and tools:"
  sha256sum acceptance_gate.md formulation.md tools/build_and_run.sh tools/gate_freeze.sh | sed 's/^/  /'
  echo
  echo "# production source at freeze time (pre-GRAD-002 state = the GRAD-001 formulation):"
  (cd $R && sha256sum src/discretization/Gradient.cpp src/mesh/MeshGeometry.cpp \
     include/cfd/mesh/MeshGeometry.hpp | sed 's/^/  /')
  echo "# pre-GRAD-002 library snapshot for base/grad001 comparison runs:"
  sha256sum $S/libcfdcore.a $HOME/m7ref/base/build/src/libcfdcore.a | sed 's/^/  /'
  echo
  echo "# GRAD-001 evidence is preserved unchanged (its own frozen gate and failure logs):"
  (cd $R/results/p12-grad-001 && sha256sum acceptance_gate.md summary.md \
     logs/04_gradient_probe_postfix.log logs/05_roundoff_floor_postfix.log \
     logs/06_predicate_behaviour_postfix.log | sed 's/^/  /')
  echo "# MESH-007's frozen gate and its original failed G6.3 evidence are unchanged:"
  (cd $R/results/p12-mesh-007 && sha256sum acceptance_gate.md \
     logs/12_gate_stage3_G1.2_G5_G6_G7_G8.log \
     logs/15_diag_g63_static_translation_BASE_and_NEW.log | sed 's/^/  /')
} > $L 2>&1
cat $L

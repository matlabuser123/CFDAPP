#!/usr/bin/env bash
# P12-GRAD-001: freeze the gradient-fix gate before any change to Gradient.cpp.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-001
L=$P/logs/03_gate_freeze.log
cd $P
{
  echo "# P12-GRAD-001 GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(cd $R && git rev-parse HEAD)"
  sha256sum acceptance_gate.md tools/measure_misalignment.cpp tools/gradient_probe.cpp \
    tools/predicate_audit.sh tools/build_and_run.sh logs/00_predicate_audit.log \
    logs/01_misalignment_prefix.log logs/02_gradient_probe_prefix.log
  echo
  echo "# Gradient.cpp is unchanged from the pre-MESH-007 reference tree at freeze time:"
  cmp -s $R/src/discretization/Gradient.cpp $HOME/m7ref/base/src/discretization/Gradient.cpp \
    && echo "  src/discretization/Gradient.cpp IDENTICAL to BASE ($(sha256sum $R/src/discretization/Gradient.cpp | cut -c1-16))" \
    || echo "  src/discretization/Gradient.cpp DIFFERS FROM BASE -- the freeze is invalid"
  echo "# the MESH-007 gate and its failed G6.3 evidence are unchanged:"
  (cd $R/results/p12-mesh-007 && sha256sum acceptance_gate.md logs/12_gate_stage3_G1.2_G5_G6_G7_G8.log \
     logs/15_diag_g63_static_translation_BASE_and_NEW.log | sed 's/^/  /')
} > $L 2>&1
cat $L

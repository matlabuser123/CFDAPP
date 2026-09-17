#!/usr/bin/env bash
# P12-MESH-007: freeze the acceptance gate and the architecture before any MESH-007 source change.
# Records sha256 of the gate, the architecture and the pre-freeze evidence, and proves that no source,
# test or input file differs from the pre-MESH-007 reference tree ($HOME/m7ref/base).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-mesh-007
L=$P/logs/05_gate_freeze.log
cd $P
{
  echo "# P12-MESH-007 GATE FREEZE; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD: $(cd $R && git rev-parse HEAD)"
  sha256sum acceptance_gate.md architecture.md tools/prefreeze_roundoff.py tools/prefreeze_solver_feasibility.cpp \
    tools/prefreeze_solver_feasibility.sh tools/m7_baseline.sh logs/00_baseline_git.log \
    logs/01_baseline_mesh006_stability.log logs/02_baseline_reference_tree.log \
    logs/03_prefreeze_roundoff_NOT_gate.log logs/04_prefreeze_solver_feasibility_NOT_gate.log
  echo
  echo "# src/include/apps/tests of the working tree vs the pre-MESH-007 reference tree (no MESH-007 source change):"
  (cd $HOME/m7ref/base && sha256sum -c --quiet $HOME/m7ref/base.src.sha256 > /dev/null 2>&1; echo "reference tree self-check rc=$?")
  cd $R
  n=0; d=0
  while read -r h f; do
    n=$((n+1)); f=${f#\*}; f=${f#./}
    if ! echo "$h  $f" | sha256sum -c --quiet > /dev/null 2>&1; then d=$((d+1)); echo "DIFFERENT $f"; fi
  done < $HOME/m7ref/base.src.sha256
  echo "files compared: $n; different: $d"
  new=$(cd $R && git ls-files -co --exclude-standard -- src include apps tests | sort | comm -13 <(cd $HOME/m7ref/base && find src include apps tests -type f | sort) - | wc -l)
  echo "new files under src/include/apps/tests since the baseline: $new"
  echo "# git status entries now: $(git status --short | wc -l) (baseline 243: 242 + results/p12-mesh-007/)"
} > $L 2>&1
cat $L

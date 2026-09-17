#!/usr/bin/env bash
# P12-MESH-006 A3: evaluator DRY RUN on the g5-investigation CLI outputs ($HOME/m6g5) and the original
# G5 level data (results/p12-mesh-006/data, read-only). Purpose: find coding errors in a3_gate.py BEFORE
# it is frozen. This is NOT the acceptance run (acceptance_gate_A3.md §7).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
D=$HOME/m6a3dry
rm -rf $D
mkdir -p $D/gtest/results/p12-mesh-006
for r in x8 x16 x24 y16 z16; do ln -s $HOME/m6g5/$r $D/$r; done
ln -s $R/results/p12-mesh-006/data $D/gtest/results/p12-mesh-006/data
cd /tmp
A3_RUNS=$D python3 $R/results/p12-mesh-006/a3/tools/a3_gate.py /tmp/a3_dry
echo "# exit: $?"

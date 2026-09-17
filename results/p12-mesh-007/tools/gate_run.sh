#!/usr/bin/env bash
# P12-MESH-007 gate run (acceptance_gate.md as amended by A1, A2), staged, stopping at the first failing
# stage (stop rule). Release test binaries; each log records the binary's sha256 and the gate's.
#   stage 1  G1.1 G1.3 G2 G3 G4       CFDMeshTests          'MeshMotion*:MeshGeometryUpdate*'
#   stage 2  G1.4 G6.1 G6.5           CFDDiscretizationTests 'AleOperators*'
#   stage 3  G1.2 G5 G6.3 G6.4 G7 G8  CFDPisoTests          'AlePiso*'
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-007/logs
T=$R/build/release/tests
cd $R
stage() {  # log binary filter label
  local log=$L/$1 bin=$2 filter=$3 label=$4
  {
    echo "# P12-MESH-007 gate stage: $label; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# gate sha256 $(sha256sum results/p12-mesh-007/acceptance_gate.md | cut -c1-16) (A1+A2: 5b45fed9...)"
    echo "# binary $(basename $bin) sha256 $(sha256sum $bin | cut -c1-16); filter '$filter'"
    $bin --gtest_filter="$filter" 2>&1
    echo "exit $?"
    echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > $log 2>&1
  echo "$label: $(grep -E '^\[  PASSED  \]|^\[  FAILED  \] [0-9]+ test' $log | tr '\n' ' ') $(grep -E '^exit' $log)"
  grep -q "^exit 0" $log
}
stage 10_gate_stage1_G1.1_G1.3_G2_G3_G4.log $T/unit/mesh/CFDMeshTests 'MeshMotion*:MeshGeometryUpdate*' \
  "stage 1 (G1.1 G1.3 G2 G3 G4)" || { echo "STOP: stage 1 failed"; exit 1; }
stage 11_gate_stage2_G1.4_G6.1_G6.5.log $T/unit/discretization/CFDDiscretizationTests 'AleOperators*' \
  "stage 2 (G1.4 G6.1 G6.5)" || { echo "STOP: stage 2 failed"; exit 1; }
stage 12_gate_stage3_G1.2_G5_G6_G7_G8.log $T/solver/piso/CFDPisoTests 'AlePiso*' \
  "stage 3 (G1.2 G5 G6.3 G6.4 G7 G8)" || { echo "STOP: stage 3 failed"; exit 1; }
echo "all three stages passed"

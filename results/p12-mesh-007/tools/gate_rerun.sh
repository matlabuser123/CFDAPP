#!/usr/bin/env bash
# P12-MESH-007 gate RERUN after P12-GRAD-002 (acceptance_gate.md as amended by A1 and A2, unchanged).
# The same three stages and filters as tools/gate_run.sh, on fresh Release binaries, writing NEW logs
# (20-22) so that the original failed run (logs 10-12, G6.3 = 2.49e-2) stays on record unchanged.
# Stops at the first failing stage (stop rule).
#   stage 1  G1.1 G1.3 G2 G3 G4       CFDMeshTests           'MeshMotion*:MeshGeometryUpdate*'
#   stage 2  G1.4 G6.1 G6.5           CFDDiscretizationTests 'AleOperators*'
#   stage 3  G1.2 G5 G6.3 G6.4 G7 G8  CFDPisoTests           'AlePiso*'
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-007/logs
T=$R/build/release/tests
cd $R || exit 1
for f in 20_rerun_stage1_G1.1_G1.3_G2_G3_G4.log 21_rerun_stage2_G1.4_G6.1_G6.5.log 22_rerun_stage3_G1.2_G5_G6_G7_G8.log; do
  [ -e "$L/$f" ] && { echo "REFUSED: $L/$f exists (a rerun is recorded once)"; exit 1; }
done
# fail closed on a stale test binary: every test binary must be newer than every source file
NT=$(find src include tests apps -type f \( -name '*.cpp' -o -name '*.hpp' \) -printf '%T@\n' | sort -rn | head -1)
for bin in $T/unit/mesh/CFDMeshTests $T/unit/discretization/CFDDiscretizationTests $T/solver/piso/CFDPisoTests; do
  bt=$(stat -c '%Y' "$bin")
  awk -v a="$bt" -v b="$NT" 'BEGIN{exit !(a<b)}' && { echo "FAIL CLOSED: stale $bin"; exit 1; }
done
stage() {  # log binary filter label
  local log=$L/$1 bin=$2 filter=$3 label=$4
  {
    echo "# P12-MESH-007 gate RERUN stage: $label; $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
    echo "# gate sha256 $(sha256sum results/p12-mesh-007/acceptance_gate.md | cut -d' ' -f1) (A1+A2 as frozen: 5b45fed9...)"
    echo "# binary $(basename $bin) sha256 $(sha256sum $bin | cut -d' ' -f1); libcfdcore.a $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
    echo "# test sources: $(sha256sum tests/solver/piso/test_ale_piso.cpp tests/unit/mesh/test_mesh_motion.cpp tests/unit/discretization/test_ale_operators.cpp tests/support/MeshMotionCases.hpp | awk '{print substr($1,1,16) " " $2}' | tr '\n' ' ')"
    echo "# filter '$filter'"
    $bin --gtest_filter="$filter" 2>&1
    echo "exit $?"
    echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > $log 2>&1
  echo "$label: $(grep -E '^\[  PASSED  \]|^\[  FAILED  \] [0-9]+ test' $log | tr '\n' ' ') $(grep -E '^exit' $log)"
  grep -q "^exit 0" $log
}
stage 20_rerun_stage1_G1.1_G1.3_G2_G3_G4.log $T/unit/mesh/CFDMeshTests 'MeshMotion*:MeshGeometryUpdate*' \
  "stage 1 (G1.1 G1.3 G2 G3 G4)" || { echo "STOP: stage 1 failed"; exit 1; }
stage 21_rerun_stage2_G1.4_G6.1_G6.5.log $T/unit/discretization/CFDDiscretizationTests 'AleOperators*' \
  "stage 2 (G1.4 G6.1 G6.5)" || { echo "STOP: stage 2 failed"; exit 1; }
stage 22_rerun_stage3_G1.2_G5_G6_G7_G8.log $T/solver/piso/CFDPisoTests 'AlePiso*' \
  "stage 3 (G1.2 G5 G6.3 G6.4 G7 G8)" || { echo "STOP: stage 3 failed"; exit 1; }
grep -E "G6.3 translating cavity|G6.4|G7.3" $L/22_rerun_stage3_G1.2_G5_G6_G7_G8.log | head -5
echo "all three stages passed"

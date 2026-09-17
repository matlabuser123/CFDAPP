#!/usr/bin/env bash
# P12-MESH-006: run one group of production-path gate levels in parallel (one process per level),
# then the group's gate test. Usage: gate_group.sh g5|g6|g7
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
# A frozen copy of the Release test binary (made once by gate_freeze.sh), so later rebuilds of the
# build tree cannot touch a running gate process.
B=$HOME/m6gate/CFDCaseIntegrationTests
L=$R/results/p12-mesh-006/logs
cd $R
run() {  # run <log> <gtest filter>
  {
    echo "# P12-MESH-006 $2"
    echo "# start: $(date -u +%Y-%m-%dT%H:%M:%SZ)  host: $(uname -srm)  cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)"
    echo "# binary: frozen copy of build/release/tests/integration/case/CFDCaseIntegrationTests, sha256 $(sha256sum $B | cut -c1-64)"
    echo "# command: CFDCaseIntegrationTests --gtest_also_run_disabled_tests --gtest_filter='$2'"
    echo "# note: levels of G5, G6 and G7 run concurrently (one process each); runtimes are wall-clock under that load"
    echo
    /usr/bin/time -f "# elapsed %e s, peak RSS %M KB" $B --gtest_also_run_disabled_tests --gtest_filter="$2" 2>&1
    echo "# exit: $?"
    echo "# end: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > "$L/$1" 2>&1
}
case "$1" in
  g5)
    run 05a_g5_duct_n8.log  'Duct3DProductionCase.DISABLED_DuctLevel8' &
    run 05b_g5_duct_n16.log 'Duct3DProductionCase.DISABLED_DuctLevel16' &
    run 05c_g5_duct_n24.log 'Duct3DProductionCase.DISABLED_DuctLevel24' &
    wait
    run 05d_g5_duct_gate.log 'Duct3DProductionCase.DISABLED_DuctGate'
    tail -40 $L/05d_g5_duct_gate.log ;;
  g6)
    run 06_g6_directional_symmetry.log 'Duct3DProductionCase.DISABLED_DirectionalSymmetry'
    tail -40 $L/06_g6_directional_symmetry.log ;;
  g7)
    run 07a_g7_cube_32.log 'LidDrivenCube3DProductionCase.DISABLED_CubeLevel32' &
    run 07b_g7_cube_48.log 'LidDrivenCube3DProductionCase.DISABLED_CubeLevel48' &
    run 07c_g7_cube_64.log 'LidDrivenCube3DProductionCase.DISABLED_CubeLevel64' &
    wait
    run 07d_g7_cube_gate.log 'LidDrivenCube3DProductionCase.DISABLED_CubeGate'
    tail -60 $L/07d_g7_cube_gate.log ;;
esac

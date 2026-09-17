#!/usr/bin/env bash
# P12-MESH-006: the focused 3D test suites (G1, G2, G3, G4 regular guard, G9.1/G9.2, 3D production
# regular tests), Release, one log.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-006/logs/02_focused_3d_tests.log
cd $R
cmake --build build/release -j8 --target CFDSimpleTests CFDDiscretizationTests CFDMMSValidationTests CFDIoTests CFDCaseIntegrationTests > $HOME/m6logs/focused_build.log 2>&1 || { tail -30 $HOME/m6logs/focused_build.log; exit 1; }
run() {  # run <binary> <filter>
  echo "## $1 --gtest_filter='$2'"
  $1 --gtest_filter="$2" 2>&1 | grep -vE "^Running main|^Note: Google|^$"
  echo "## exit: ${PIPESTATUS[0]}"
  echo
}
{
  echo "# P12-MESH-006 focused 3D tests (Release), $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# build warnings in this rebuild: $(grep -c 'warning:' $HOME/m6logs/focused_build.log)"
  echo
  run build/release/tests/solver/simple/CFDSimpleTests 'SIMPLE3D.*'
  run build/release/tests/unit/discretization/CFDDiscretizationTests 'Operators3DTest.*'
  run build/release/tests/integration/mms/CFDMMSValidationTests 'MMSSimple3DTest.*'
  run build/release/tests/unit/io/CFDIoTests 'Case3DTest.*'
  run build/release/tests/integration/case/CFDCaseIntegrationTests 'Duct3DProductionCase.*:LidDrivenCube3DProductionCase.*'
} > $L 2>&1
grep -E "^## |PASSED|FAILED|warnings" $L

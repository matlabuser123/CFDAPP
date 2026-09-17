#!/usr/bin/env bash
# P12-MESH-006 G4: the pre-registered 3D SIMPLE MMS gate study (Release, from the repo root).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-006/logs/04_g4_mms_simple3d.log
cd $R
cmake --build build/release -j16 --target CFDMMSValidationTests > $HOME/m6logs/g4build.log 2>&1 || { tail -30 $HOME/m6logs/g4build.log; exit 1; }
{
  echo "# P12-MESH-006 G4 -- 3D production SIMPLE MMS gate study (acceptance_gate.md G4)"
  echo "# date: $(date -u +%Y-%m-%dT%H:%M:%SZ)  host: $(uname -srm)  cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)"
  echo "# build: build/release (Release), warnings in rebuild: $(grep -c 'warning:' $HOME/m6logs/g4build.log)"
  echo "# command: CFDMMSValidationTests --gtest_also_run_disabled_tests --gtest_filter='MMSSimple3DTest.*'"
  echo
  /usr/bin/time -f "# elapsed %e s, peak RSS %M KB" build/release/tests/integration/mms/CFDMMSValidationTests \
    --gtest_also_run_disabled_tests --gtest_filter='MMSSimple3DTest.*' 2>&1
  echo "# exit: ${PIPESTATUS[0]}"
} > $L 2>&1
tail -60 $L

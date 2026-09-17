#!/usr/bin/env bash
# P12-MESH-007 G2.3: dump the SN2/SN3 moved geometry with the (DISABLED) evidence test, then check it
# with the independent Python implementation, then the instrument self-test (must FAIL).
# usage: g23_run.sh <log-name> <data-dir>
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-mesh-007
LOG=$P/logs/$1
D=$2
mkdir -p "$D"
BIN=$R/build/release/tests/unit/mesh/CFDMeshTests
{
  echo "# P12-MESH-007 G2.3 $(date -u +%Y-%m-%dT%H:%M:%SZ); binary $(sha256sum $BIN | cut -d' ' -f1); libcfdcore.a $(sha256sum $R/build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# instrument $(sha256sum $P/tools/independent_geometry.py | cut -d' ' -f1); data dir $D"
  M7_DATA_DIR=$D $BIN --gtest_also_run_disabled_tests --gtest_filter='MeshMotionEvidence.DISABLED_DumpSinusoidalGeometryForIndependentCheck' 2>&1 | grep -E "dumped|OK|FAILED|PASSED"
  (cd $D && sha256sum geometry_*.json)
  python3 $P/tools/independent_geometry.py "$D"
  echo "independent check exit $?"
  echo "## instrument self-test (a 1e-10 relative volume perturbation must be reported FAIL)"
  python3 $P/tools/independent_geometry.py "$D" --self-test
  echo "self-test exit $? (must be 1)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG

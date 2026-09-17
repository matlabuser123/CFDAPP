#!/usr/bin/env bash
# P12-MESH-006 Amendment A3: the fresh G5 / G6 acceptance run (acceptance_gate_A3.md §7).
# Frozen together with the A3 document, the expected values and the evaluator (a3/logs/00_freeze.log).
#   1. rebuild the Release CLI and CFDCaseIntegrationTests from the current tree; freeze copies under
#      $HOME/m6a3/bin (the build tree is on /mnt/c) and record their sha256;
#   2. fresh production CLI runs: x-duct n = 8, 16, 24; y- and z-duct n = 16 (cases generated from
#      cases/duct_3d by g5-investigation/tools/gen_duct_case.py, unchanged);
#   3. fresh ProjectRunner level runs of the unchanged G5 test code (DISABLED_DuctLevel8/16/24) in a
#      relocated working directory, so the original results/p12-mesh-006/data files are never touched;
#   4. the A3 evaluator a3/tools/a3_gate.py.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
A=$R/results/p12-mesh-006/a3
W=$HOME/m6a3
L=$A/logs
set -u
mkdir -p $W/bin $L
{
  echo "# A3 build: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  cd $R && cmake --build build/release -j16 --target cfdapp CFDCaseIntegrationTests > $W/build.log 2>&1
  echo "# build exit: $?; warnings: $(grep -c 'warning:' $W/build.log)"
  cp $R/build/release/apps/cli/cfdapp $R/build/release/tests/integration/case/CFDCaseIntegrationTests $W/bin/
  sha256sum $W/bin/cfdapp $W/bin/CFDCaseIntegrationTests
} > $L/01_build_and_freeze_binaries.log 2>&1
cat $L/01_build_and_freeze_binaries.log

cli() {  # cli <name> <n> <axis>
  python3 $R/results/p12-mesh-006/g5-investigation/tools/gen_duct_case.py $W/$1 $2 $3
  {
    echo "# A3 fresh CLI run '$1' (n=$2, axis $3); start $(date -u +%Y-%m-%dT%H:%M:%SZ); host $(uname -srm)"
    echo "# command: $W/bin/cfdapp --case $W/$1   (cfdapp sha256 $(sha256sum $W/bin/cfdapp | cut -c1-64))"
    echo
    /usr/bin/time -f "# elapsed %e s, peak RSS %M KB" $W/bin/cfdapp --case $W/$1 2>&1
    echo "# exit: $?"
    echo "# fields.csv sha256 $(sha256sum $W/$1/results/fields.csv | cut -c1-64)"
    echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > $L/02_cli_$1.log 2>&1
}
level() {  # level <n>
  {
    echo "# A3 fresh ProjectRunner level run n=$1 (unchanged G5 test code); start $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# cwd $W/gtest (cases -> $R/cases); writes $W/gtest/results/p12-mesh-006/data/duct3d_n$1.json"
    echo "# command: CFDCaseIntegrationTests --gtest_also_run_disabled_tests --gtest_filter=Duct3DProductionCase.DISABLED_DuctLevel$1"
    echo "#          (sha256 $(sha256sum $W/bin/CFDCaseIntegrationTests | cut -c1-64))"
    echo
    cd $W/gtest && /usr/bin/time -f "# elapsed %e s, peak RSS %M KB" $W/bin/CFDCaseIntegrationTests \
      --gtest_also_run_disabled_tests --gtest_filter=Duct3DProductionCase.DISABLED_DuctLevel$1 2>&1
    echo "# exit: $?"
    echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > $L/03_level_n$1.log 2>&1
}
rm -rf $W/gtest && mkdir -p $W/gtest && ln -s $R/cases $W/gtest/cases
for spec in x8:8:x x16:16:x x24:24:x y16:16:y z16:16:z; do
  IFS=: read -r name n axis <<< "$spec"
  cli $name $n $axis &
done
for n in 8 16 24; do level $n & done
wait
{
  echo "# A3 evaluation: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# a3_gate.py sha256 $(sha256sum $A/tools/a3_gate.py | cut -c1-64); frozen_expected.json sha256 $(sha256sum $A/data/frozen_expected.json | cut -c1-64)"
  echo "# command: A3_RUNS=$W python3 $A/tools/a3_gate.py $A/data/a3_gate_result"
  echo
  cd /tmp && A3_RUNS=$W python3 $A/tools/a3_gate.py $A/data/a3_gate_result
  echo "# exit: $?"
} > $L/04_a3_gate.log 2>&1
tail -5 $L/04_a3_gate.log

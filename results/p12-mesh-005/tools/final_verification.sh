#!/usr/bin/env bash
# P12-MESH-005 final verification on the final sources (after the post-verification comment edit of
# src/discretization/Gradient.cpp, logs/05):
#   1. the staged focused tests (tools/focused.sh, Release) -> logs/17; stop on the first failed stage;
#   2. full regression, build/release (Release -O3, GUI off)  -> logs/15;
#   3. full regression, build/debug (Debug, GUI on, offscreen) -> logs/16.
# Sequential (never two ctest runs at once). git status before/after is kept for the G5 classification.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-005/logs
W=$HOME/m5final
mkdir -p $W
cd $R
rm -f $W/done
git status --short > $W/status_before.txt
{
  echo "# P12-MESH-005 staged focused tests on the FINAL binaries (Release -O3), rerun of logs/11"
  echo "# test binaries built $(stat -c %y build/release/tests/unit/mesh/CFDMeshTests | cut -c1-19) (after the last source edit)"
  bash $R/results/p12-mesh-005/tools/focused.sh
  echo "focused exit $?"
} > $L/17_focused_tests_final_binaries.log 2>&1
if ! grep -q "^focused exit 0" $L/17_focused_tests_final_binaries.log; then
  echo "FOCUSED-FAILED" > $W/done
  exit 1
fi
{
  echo "# P12-MESH-005 full regression, build/release (Release -O3, GUI off), $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# HEAD $(git rev-parse HEAD) + uncommitted P12-MESH-001..004 working tree + P12-MESH-005 (3D foundation)"
  echo "# build: $(grep -c 'warning:' $HOME/m5logs/final_build_release.log) warnings ($HOME/m5logs/final_build_release.log)"
  (cd build/release && ctest -j16 --output-on-failure 2>&1)
  echo "release ctest exit $?"
} > $L/15_full_regression_release.log 2>&1
git status --short > $W/status_after_release.txt
{
  echo "# P12-MESH-005 full regression, build/debug (Debug, GUI on, Qt offscreen), $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# HEAD $(git rev-parse HEAD) + uncommitted P12-MESH-001..004 working tree + P12-MESH-005 (3D foundation)"
  echo "# build: $(grep -c 'warning:' $HOME/m5logs/final_build_debug.log) warnings ($HOME/m5logs/final_build_debug.log)"
  (cd build/debug && QT_QPA_PLATFORM=offscreen ctest -j16 --output-on-failure 2>&1)
  echo "debug ctest exit $?"
} > $L/16_full_regression_debug_gui.log 2>&1
git status --short > $W/status_after.txt
echo "FULL-REGRESSION-DONE" > $W/done

#!/usr/bin/env bash
# P12-DIFF-002 W4: build and run the hand-derived matrix/coefficient tests, and record the state of
# the test source that produced the result.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
L=$P/logs/10_gate_W4.log
cd $R
{
  echo "# P12-DIFF-002 W4 (hand-derived matrix tests); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  sha256sum tests/unit/discretization/test_boundary_reconstruction.cpp \
    tests/unit/discretization/CMakeLists.txt build/release/src/libcfdcore.a | sed 's/^/  /'
  echo
  cmake --build build/release --target CFDDiscretizationTests -j"$(nproc)" 2>&1 | tail -3
  echo
  ./build/release/tests/unit/discretization/CFDDiscretizationTests \
    --gtest_filter='BoundaryInwardStencil*:BoundaryReconstruction*' 2>&1
  echo "exit $?"
} > $L 2>&1
cat $L

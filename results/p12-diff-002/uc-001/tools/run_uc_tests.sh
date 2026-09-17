#!/usr/bin/env bash
# UC-001: run the three U-C Poiseuille tests against the working-tree build and record verbatim
# failure output. Usage: run_uc_tests.sh <log-name> [gtest-filter]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uc-001
mkdir -p "$P/logs"
FILTER=${2:-PoiseuilleValidation.Profile:PoiseuilleValidation.MassFlow:PoiseuilleValidation.PressureDrop:PoiseuilleValidation.ProductionGridConvergence}
cd "$R" || exit 1
cmake --build build/release --target CFDPoiseuilleValidationTests -j"$(nproc)" > "$P/logs/$1.build" 2>&1 || {
  echo "BUILD FAILED"; tail -40 "$P/logs/$1.build"; exit 1; }
{
  echo "# UC-001 poiseuille tests; filter $FILTER"
  echo "# libcfdcore.a sha256 $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  ./build/release/tests/integration/poiseuille/CFDPoiseuilleValidationTests --gtest_filter="$FILTER"
  echo "exit $?"
} > "$P/logs/$1" 2>&1
grep -E "^\[|Failure|error:|which is|Actual|Expected|bound|exit " "$P/logs/$1" | head -120

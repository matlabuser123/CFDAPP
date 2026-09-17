#!/usr/bin/env bash
# P12-GRAD-002 A3 freeze. Refuses while a build or ctest runs, or if A3 is already frozen.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
LOG=$R/results/p12-grad-002/a3/logs/00_freeze.log
cd $R || exit 1
if pgrep -x ctest > /dev/null || pgrep -f "cmake --build" > /dev/null; then echo "REFUSED: a build or ctest is running"; exit 1; fi
[ -e "$LOG" ] && { echo "REFUSED: A3 already frozen"; exit 1; }
{
  echo "# P12-GRAD-002 A3 freeze $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
  sha256sum results/p12-grad-002/acceptance_gate_A3.md results/p12-grad-002/acceptance_gate_A2.md \
            results/p12-grad-002/a2/logs/00_freeze.log results/p12-grad-002/a2/dryrun.md
  echo "## A3 instruments and dry-run"
  sha256sum results/p12-grad-002/a3/tools/*.py results/p12-grad-002/a3/tools/*.sh results/p12-grad-002/a3/logs/dry_*
  echo "## A2 instruments used by the carried-over steps (added after the A2 freeze: run_regression.sh, classify_scope.py, step2.sh, clean_rebuild.sh)"
  sha256sum results/p12-grad-002/a2/tools/run_regression.sh results/p12-grad-002/a2/tools/classify_scope.py \
            results/p12-grad-002/a2/tools/step2.sh results/p12-grad-002/a2/tools/clean_rebuild.sh \
            results/p12-grad-002/a2/tools/run_suite.sh results/p12-grad-002/a2/tools/dry_c8_c11.sh \
            results/p12-grad-002/a2/tools/c8_categorize.py results/p12-grad-002/a2/tools/c11b_fields.py
  echo "## the A2 failure record (unchanged)"
  sha256sum results/p12-grad-002/a2/logs/fresh_c10_selftest_mutated.log results/p12-grad-002/a2/logs/dry_c10_selftest_mutated.log
  echo "## production and tests"
  echo "src/ + include/ tree: $(find src include -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
  sha256sum tests/unit/discretization/test_gradient_boundary_consistency.cpp tests/unit/discretization/CMakeLists.txt \
            build/release/src/libcfdcore.a
} > $LOG 2>&1
chmod a-w $LOG
cat $LOG

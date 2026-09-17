#!/usr/bin/env bash
# P12-MESH-006: apply clang-format-18 to the MESH-006 files the dry run flagged, keep pre-format
# copies, and show that the change is layout only: every file's token stream (whitespace removed)
# is identical before and after.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$HOME/m6format_pre
cd $R
FILES="apps/cli/main.cpp apps/gui/tests/test_case_editing.cpp src/app/VisualizationSnapshot.cpp src/io/CaseWriter.cpp
src/io/ResultExporter.cpp src/io/case/MeshConfigParser.cpp src/io/case/SolverConfigParser.cpp
src/physics/ContinuityEquation.cpp src/pressure_velocity/PressureCorrectionEquation.cpp
src/pressure_velocity/SIMPLE.cpp src/solver/SolverRobustness.cpp tests/integration/case/test_3d_production_cases.cpp
tests/integration/mms/test_mms_simple3d.cpp tests/solver/simple/test_simple3d.cpp
tests/unit/discretization/test_operators3d.cpp tests/unit/io/test_case3d.cpp"
rm -rf $P; mkdir -p $P
for f in $FILES; do mkdir -p $P/$(dirname $f); cp $f $P/$f; done
clang-format-18 -i $FILES
echo "# formatted: $(echo $FILES | wc -w) files with clang-format-18 ($(clang-format-18 --version))"
for f in $FILES; do
  changed=$(diff $P/$f $f | grep -c '^[<>]')
  same_tokens=$(cmp -s <(tr -d ' \t\n' < $P/$f) <(tr -d ' \t\n' < $f) && echo "tokens identical" || echo "TOKENS DIFFER")
  echo "$f: $changed changed lines; $same_tokens"
done

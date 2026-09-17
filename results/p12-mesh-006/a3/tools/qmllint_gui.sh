#!/usr/bin/env bash
# P12-MESH-006: qmllint (Qt 6.2.4, /usr/lib/qt6/bin/qmllint -- /usr/bin/qmllint is a qtchooser stub)
# of the QML files MESH-006 changed, pre-MESH-006 snapshot ($HOME/m6ref/base) vs current. As in
# MESH-003/004 (results/p12-mesh-004/logs/24): qmllint cannot resolve the C++ context property
# (simulationController), so "Unqualified access" warnings are the existing project-wide pattern;
# the check is 0 errors and no new warning kind.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$HOME/m6ref/base
Q=/usr/lib/qt6/bin/qmllint
echo "# qmllint $($Q --version 2>&1 | head -1); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
for f in MeshEditor.qml BoundaryEditor.qml ResultsPage.qml; do
  for tree in "$B" "$R"; do
    out=$($Q "$tree/apps/gui/qml/$f" 2>&1); rc=$?
    warnings=$(echo "$out" | grep -c '^Warning')
    errors=$(echo "$out" | grep -c '^Error')
    kinds=$(echo "$out" | grep -E '^(Warning|Error)' | sed -E 's/^(Warning|Error): [^:]*:[0-9]+:[0-9]+: //; s/"[^"]*"/"X"/g' \
            | sed -E 's/^(Unqualified access).*/\1/' | sort | uniq -c | tr '\n' ';')
    echo "$tree/apps/gui/qml/$f: exit $rc | warnings $warnings | errors $errors | kinds: $kinds"
  done
done

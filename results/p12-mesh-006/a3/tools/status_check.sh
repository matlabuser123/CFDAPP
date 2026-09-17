#!/usr/bin/env bash
# P12-MESH-006: working-tree status after the verification runs and the G10.5 restore, compared with the
# status taken just before the full regression ($HOME/m6final/status_before.txt, full_regression.sh).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
W=$HOME/m6final
cd $R
git status --short > $W/status_final.txt
echo "# status entries: before full regression $(wc -l < $W/status_before.txt); now $(wc -l < $W/status_final.txt)"
echo "# entries added since (outside results/p12-mesh-006/):"
comm -13 <(sort $W/status_before.txt) <(sort $W/status_final.txt) | grep -v "results/p12-mesh-006/" || echo "  (none)"
echo "# entries removed since:"
comm -23 <(sort $W/status_before.txt) <(sort $W/status_final.txt) || true

#!/usr/bin/env bash
# P12-MESH-007 G10 citation support: hash of every build input (generated */results/ outputs excluded).
# usage: tree_hash.sh [label]  -- appends to logs/24_build_input_tree_hash.log
L=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-mesh-007/logs/24_build_input_tree_hash.log
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
{
echo "# ${1:-check} $(date -u +%Y-%m-%dT%H:%M:%SZ)"
git ls-files -co --exclude-standard -- src include tests apps cmake CMakeLists.txt CMakePresets.json cases \
  | grep -v -E '(^|/)results/' | sort > /tmp/m7_treelist.txt
echo "files $(wc -l < /tmp/m7_treelist.txt)"
echo "hash $(tr '\n' '\0' < /tmp/m7_treelist.txt | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
echo "newest non-results build input: $(tr '\n' '\0' < /tmp/m7_treelist.txt | xargs -0 stat -c '%Y %n' | sort -rn | head -1 | awk '{print strftime("%Y-%m-%dT%H:%M:%SZ",$1,1), $2}')"
} >> "$L" 2>&1
tail -4 "$L"

#!/usr/bin/env bash
# P12-MESH-007 G10 reuse condition 1 (user authorization relayed on 2026-09-17): the full
# build-input tree must be unchanged since the GRAD-002 A3 regression built it.
# Build inputs: every file under src, include, tests, apps, cmake and cuda, excluding
# tests/data/cases/*/results/* and *.pyc, plus CMakeLists.txt and CMakePresets.json.
# The definition and the reference value (34cd655d..., 733 files, 2026-09-17T15:39:39Z) are the
# read-only audit session's (cfdapp-47, fulltree.sh), copied here unchanged.
# usage: g10_build_input_hash.sh <label>   -- appends to logs/60_g10_build_input_hash.log
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
LOG=results/p12-mesh-007/logs/60_g10_build_input_hash.log
{
echo "## ${1:-check}"
date -u +%FT%TZ
list() { find src include tests apps cmake cuda -type f ! -path 'tests/data/cases/*/results/*' ! -name '*.pyc' -print0 2>/dev/null; printf '%s\0' CMakeLists.txt CMakePresets.json; }
echo "full build-input tree: $(list | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)  files $(list | tr -cd '\0' | wc -c)"
echo "newest build input:"; list | xargs -0 stat -c '%Y %n' | sort -rn | head -3 | while read -r t f; do echo "  $(date -u -d @$t +%FT%TZ) $f"; done
echo "release build log start: $(head -1 results/p12-grad-002/a2/logs/regr_02_release.log)"
} >> $LOG 2>&1
tail -8 $LOG

#!/usr/bin/env bash
# DRIFT-001 round 4: the fine StructuredQuad grids (216x27, 256x32) fail with Rhie-Chow AND (per
# W8-INV-001) with the linear flux. Which library / setting makes them fail?  "gate" mode: the
# committed settings, stopping at convergence or the 3000-iteration cap or a solver failure.
set -u
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/drift-001/tools
TREE=repo bash "$T/run.sh" "00_build_repo_r4.log" sq 16 4 rc+n0 gate
pids=()
for g in 216:27 256:32; do
  n="${g/:/x}"
  for tree in repo nodiff pregrad; do
    for v in base rc; do
      TREE=$tree bash "$T/run.sh" "r4_sq_${n}_${tree}_${v}.log" sq "${g%%:*}" "${g##*:}" "$v" gate & pids+=($!)
    done
  done
  for v in n0 up ls a5 rc+n0 rc+up rc+ls rc+a5 rc+a3 rc+n2; do
    TREE=repo bash "$T/run.sh" "r4_sq_${n}_repo_${v}.log" sq "${g%%:*}" "${g##*:}" "$v" gate & pids+=($!)
  done
done
for p in "${pids[@]}"; do wait "$p"; done
echo "round4 done $(date -u +%Y-%m-%dT%H:%M:%SZ)"

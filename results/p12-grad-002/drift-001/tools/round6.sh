#!/usr/bin/env bash
# DRIFT-001 round 6 (evidence for the W8 re-derivation, all with the Rhie-Chow flux):
#  (a) the W8 grids on every control library: two-point (nodiff), far-cell x2, sign flip
#  (b) the non-orthogonal-correction activation test's uncorrected recipe (N = 0), and the
#      corrected one, on the test grids, linear and Rhie-Chow
set -u
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/drift-001/tools
for tree in repo nodiff farx2 signflip; do
  TREE=$tree bash "$T/run.sh" "00_build_${tree}_r6.log" sq 16 4 rc gate || exit 1
done
pids=()
for tree in nodiff farx2 signflip; do
  for g in 64:8 96:12 144:18; do
    TREE=$tree bash "$T/run.sh" "r6_sq_${g/:/x}_${tree}_rc.log" sq "${g%%:*}" "${g##*:}" rc ckpt 3000 & pids+=($!)
  done
  for g in 8:20 12:30 18:45; do
    TREE=$tree bash "$T/run.sh" "r6_mb_${g/:/x}_${tree}_rc.log" mb "${g%%:*}" "${g##*:}" rc ckpt 3000 & pids+=($!)
  done
done
for g in 64:8 96:12 144:18; do
  for v in n0 rc+n0; do
    TREE=repo bash "$T/run.sh" "r6_sq_${g/:/x}_repo_${v}.log" sq "${g%%:*}" "${g##*:}" "$v" gate & pids+=($!)
  done
done
for p in "${pids[@]}"; do wait "$p"; done
echo "round6 done $(date -u +%Y-%m-%dT%H:%M:%SZ)"

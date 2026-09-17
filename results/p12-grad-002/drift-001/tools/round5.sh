#!/usr/bin/env bash
# DRIFT-001 round 5: the StructuredQuad refinement families with the Rhie-Chow flux and N = 2
# (the setting that converges on the fine grids; round 4 shows N and alpha do not move the fixed
# point), on the distorted mapping AND on the orthogonal control mapping (sq0), whose dp/dx error
# must equal UC-001's exact discrete value 1/(2 ny^2 + 1) if the extraction is sound.
set -u
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/drift-001/tools
TREE=repo bash "$T/run.sh" "00_build_repo_r5.log" sq0 16 4 rc+n2 gate
pids=()
for k in sq sq0; do
  for g in 64:8 96:12 144:18 216:27 128:16 256:32; do
    TREE=repo bash "$T/run.sh" "r5_${k}_${g/:/x}_rc+n2.log" "$k" "${g%%:*}" "${g##*:}" rc+n2 ckpt 4000 & pids+=($!)
  done
  TREE=repo bash "$T/run.sh" "r5_${k}_512x64_rc+n2.log" "$k" 512 64 rc+n2 gate & pids+=($!)
done
# orthogonal control with the COMMITTED settings (linear flux, N = 1): UC-001's reference system
for g in 64:8 96:12 144:18; do
  TREE=repo bash "$T/run.sh" "r5_sq0_${g/:/x}_base.log" sq0 "${g%%:*}" "${g##*:}" base ckpt 20000 & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
echo "round5 done $(date -u +%Y-%m-%dT%H:%M:%SZ)"

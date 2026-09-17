#!/usr/bin/env bash
# DRIFT-001 round 5b: rerun of round 5's orthogonal control (sq0), whose round-5 logs are INVALID
# (the probe failed to compile and a stale binary ran them as the MultiBlock case; the runner now
# fails closed). Plus gate-only re-runs of the distorted family to record the split-window dp/dx.
set -u
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/drift-001/tools
TREE=repo bash "$T/run.sh" "00_build_repo_r5b.log" sq0 16 4 rc+n2 gate || exit 1
pids=()
for g in 64:8 96:12 144:18 216:27 128:16 256:32; do
  TREE=repo bash "$T/run.sh" "r5b_sq0_${g/:/x}_rc+n2.log" sq0 "${g%%:*}" "${g##*:}" rc+n2 ckpt 4000 & pids+=($!)
  TREE=repo bash "$T/run.sh" "r5b_sq_${g/:/x}_rc+n2_gate.log" sq "${g%%:*}" "${g##*:}" rc+n2 gate & pids+=($!)
done
for g in 64:8 96:12 144:18; do
  TREE=repo bash "$T/run.sh" "r5b_sq0_${g/:/x}_base.log" sq0 "${g%%:*}" "${g##*:}" base ckpt 20000 & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
echo "round5b done $(date -u +%Y-%m-%dT%H:%M:%SZ)"

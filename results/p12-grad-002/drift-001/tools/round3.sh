#!/usr/bin/env bash
# DRIFT-001 round 3: with the Rhie-Chow face flux (the variant that removes the odd-even mode),
# are the W8 refinement families stationary and asymptotic?  Authoritative library only.
#   SQ  r = 1.5 family 64x8, 96x12, 144x18, 216x27, 324x40.5(no) -> 64,96,144,216 ; r = 2: 64,128,256,512
#   MB  r = 1.5 family 8x20, 12x30, 18x45 ; r = 2: 8x20, 16x40, 32x80 ; r = 4/3: 12x30, 16x40, 24x60(x1.5)
# "ckpt 3000": committed gate PLUS the state after 3000 iterations at an unreachable tolerance, so
# every row carries its own iterative-error check.
set -u
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/drift-001/tools
pids=()
for g in 64:8 96:12 144:18 216:27 128:16 256:32 512:64; do
  TREE=repo bash "$T/run.sh" "r3_sq_${g/:/x}_rc.log" sq "${g%%:*}" "${g##*:}" rc ckpt 3000 & pids+=($!)
done
for g in 8:20 12:30 18:45 16:40 24:60 32:80 36:90; do
  TREE=repo bash "$T/run.sh" "r3_mb_${g/:/x}_rc.log" mb "${g%%:*}" "${g##*:}" rc ckpt 3000 & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
echo "round3 done $(date -u +%Y-%m-%dT%H:%M:%SZ)"

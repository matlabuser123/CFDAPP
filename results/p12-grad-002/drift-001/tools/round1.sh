#!/usr/bin/env bash
# DRIFT-001 round 1: coarse grids (SQ 64x8, MB 8x20), three libraries, committed settings, plus
# single-setting variants on the authoritative library. All jobs in parallel.
set -u
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/drift-001/tools
# build each tree's probe once, serially, before fanning out
for tree in repo nodiff pregrad; do TREE=$tree bash "$T/run.sh" "00_build_$tree.log" sq 8 2 base gate; done
pids=()
for tree in repo nodiff pregrad; do
  TREE=$tree bash "$T/run.sh" "r1_sq64_${tree}_base_hist.log" sq 64 8 base hist 40000 1000 & pids+=($!)
  TREE=$tree bash "$T/run.sh" "r1_sq64_${tree}_base_ckpt.log" sq 64 8 base ckpt 5000,10000,20000,20001,40000 & pids+=($!)
  TREE=$tree bash "$T/run.sh" "r1_mb8_${tree}_base_hist.log"  mb 8 20 base hist 40000 1000 & pids+=($!)
  TREE=$tree bash "$T/run.sh" "r1_mb8_${tree}_base_ckpt.log"  mb 8 20 base ckpt 5000,10000,20000,20001,40000 & pids+=($!)
done
for v in rc n0 n2 ls up bicg tight; do
  TREE=repo bash "$T/run.sh" "r1_sq64_repo_${v}_hist.log" sq 64 8 "$v" hist 20000 1000 & pids+=($!)
  TREE=repo bash "$T/run.sh" "r1_sq64_repo_${v}_ckpt.log" sq 64 8 "$v" ckpt 5000,20000,20001 & pids+=($!)
  TREE=repo bash "$T/run.sh" "r1_mb8_repo_${v}_hist.log"  mb 8 20 "$v" hist 20000 1000 & pids+=($!)
  TREE=repo bash "$T/run.sh" "r1_mb8_repo_${v}_ckpt.log"  mb 8 20 "$v" ckpt 5000,20000,20001 & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
echo "round1 done $(date -u +%Y-%m-%dT%H:%M:%SZ)"

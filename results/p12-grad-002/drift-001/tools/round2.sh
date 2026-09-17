#!/usr/bin/env bash
# DRIFT-001 round 2:
#  (a) committed Cartesian cases, unchanged: does the linear-flux odd-even mode also evolve there?
#  (b) the W8 finest grids (SQ 144x18, MB 18x45): gate vs 20000 iterations, linear vs Rhie-Chow,
#      on the three libraries.
set -u
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/drift-001/tools
TREE=repo bash "$T/run.sh" "00_build_repo_r2.log" dir:cases/lid_driven_cavity 20 0 base gate
pids=()
for spec in poiseuille_flow:64 lid_driven_cavity:20 channel_transpiration_graded:48 lid_driven_cavity_40x40:40; do
  c=${spec%%:*}; nx=${spec##*:}
  for v in base rc; do
    TREE=repo bash "$T/run.sh" "r2_${c}_${v}_ckpt.log" "dir:cases/$c" "$nx" 0 "$v" ckpt 5000,20000,20001 & pids+=($!)
  done
done
for tree in repo nodiff pregrad; do
  TREE=$tree bash "$T/run.sh" "r2_sq144_${tree}_base_ckpt.log" sq 144 18 base ckpt 20000 & pids+=($!)
  TREE=$tree bash "$T/run.sh" "r2_mb18_${tree}_base_ckpt.log" mb 18 45 base ckpt 20000 & pids+=($!)
  TREE=$tree bash "$T/run.sh" "r2_sq144_${tree}_rc_ckpt.log" sq 144 18 rc ckpt 3000 & pids+=($!)
  TREE=$tree bash "$T/run.sh" "r2_mb18_${tree}_rc_ckpt.log" mb 18 45 rc ckpt 3000 & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
echo "round2 done $(date -u +%Y-%m-%dT%H:%M:%SZ)"

#!/usr/bin/env bash
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/a2/tools
SUF=${1:-dry}
for tree in repo nograd grad001; do
  bash $T/run_a2.sh a2_c9 ${SUF}_c9_$tree.log $tree > /dev/null &
done
wait
for tree in repo nograd grad001; do echo "== $tree"; grep -E "C3b|C6s|FAIL|INVALID|BUILD|exit" $T/../logs/${SUF}_c9_$tree.log | head -30; done

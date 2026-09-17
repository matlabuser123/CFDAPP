#!/usr/bin/env bash
# A2 dry-run of the 3D/skewed linear-field probe on three libraries, in parallel.
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/a2/tools
bash $T/run_a2.sh a2_3d dry_3d_repo.log repo > /dev/null &
bash $T/run_a2.sh a2_3d dry_3d_nograd.log nograd > /dev/null &
bash $T/run_a2.sh a2_3d dry_3d_grad001.log grad001 > /dev/null &
wait
cd $T/../logs && grep -h "K=4" dry_3d_repo.log dry_3d_nograd.log dry_3d_grad001.log | sed 's/  */ /g'

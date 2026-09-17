#!/usr/bin/env bash
# run a2_prodmesh against repo and nograd (cwd must be a tree with cases/: the repo)
T=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/p12-grad-002/a2/tools
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
bash $T/run_a2.sh a2_prodmesh dry_prodmesh_repo.log repo > /dev/null &
bash $T/run_a2.sh a2_prodmesh dry_prodmesh_nograd.log nograd > /dev/null &
wait
cd $T/../logs && for f in dry_prodmesh_repo.log dry_prodmesh_nograd.log; do echo "== $f"; grep -E "TRUNC|FAIL|exit" $f; done

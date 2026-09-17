#!/usr/bin/env bash
# P12-MESH-006: the pre-MESH-006 reference tree. Copies every tracked and untracked-not-ignored file of the
# working tree (the end state of MESH-005) to $HOME/m6ref/base, hashes src/include, and builds it in Release with
# the same options as build/release (Ninja, BUILD_TESTING ON, GUI/CUDA/OpenMP/MPI off, benchmarks on).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$HOME/m6ref/base
L=$R/results/p12-mesh-006/logs/00_baseline_and_snapshot.log
rm -rf $B; mkdir -p $B
cd $R
{
  echo "# P12-MESH-006 baseline (before any MESH-006 change); $(date -u)"
  echo "\$ git rev-parse HEAD"; git rev-parse HEAD
  echo "\$ git status --short"; git status --short
  echo "\$ git diff --stat"; git diff --stat
  echo "\$ git diff --name-status"; git diff --name-status
  echo
  echo "# snapshot: git ls-files -co --exclude-standard -> $B"
  git ls-files -co --exclude-standard -z | xargs -0 -I{} cp --parents -p {} $B/ 2>&1 | head
  echo "files copied: $(cd $B && find . -type f | wc -l)"
  (cd $B && find src include -type f | sort | xargs sha256sum) > $HOME/m6ref/base.src.sha256
  echo "src/include files hashed: $(wc -l < $HOME/m6ref/base.src.sha256) -> \$HOME/m6ref/base.src.sha256"
} > $L 2>&1
cd $B
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCFDAPP_BUILD_GUI=OFF \
  -DCFDAPP_ENABLE_CUDA=OFF -DCFDAPP_ENABLE_OPENMP=OFF -DCFDAPP_ENABLE_MPI=OFF -DCFDAPP_BUILD_BENCHMARKS=ON \
  > $HOME/m6ref/base.configure.log 2>&1 || { echo "configure failed" >> $L; exit 1; }
cmake --build build -j16 > $HOME/m6ref/base.build.log 2>&1
echo "base build rc=$? warnings=$(grep -c 'warning:' $HOME/m6ref/base.build.log); cfdapp sha256 $(sha256sum build/apps/cli/cfdapp | cut -c1-16)" >> $L
tail -1 $L

#!/usr/bin/env bash
# P12-MESH-006: full builds of the final sources -- build/release (Release -O3, GUI off) and
# build/debug (Debug, GUI on) -- with warning counts, sequential.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-006/a3/logs
cd $R
{
  echo "# P12-MESH-006 final builds, $(date -u +%Y-%m-%dT%H:%M:%SZ); HEAD $(git rev-parse HEAD) + working tree"
  cmake --build build/release -j16 > $HOME/m6logs/final_build_release.log 2>&1
  echo "release build exit $?; warnings $(grep -c 'warning:' $HOME/m6logs/final_build_release.log)"
  grep 'warning:' $HOME/m6logs/final_build_release.log | sed 's/.*CFDApp\///' | sort | uniq -c
  cmake --build build/debug -j16 > $HOME/m6logs/final_build_debug.log 2>&1
  echo "debug build exit $?; warnings $(grep -c 'warning:' $HOME/m6logs/final_build_debug.log)"
  grep 'warning:' $HOME/m6logs/final_build_debug.log | sed 's/.*CFDApp\///' | sort | uniq -c
  echo "# cfdapp sha256 $(sha256sum build/release/apps/cli/cfdapp | cut -c1-64)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L/12_final_builds.log 2>&1
cat $L/12_final_builds.log

#!/usr/bin/env bash
# P12-MESH-007: build and run the G6.3 step-1 diagnosis against build/release (evidence only).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
W=$HOME/m7diag; mkdir -p $W
c++ -std=c++20 -O3 -DNDEBUG -I$R/include -I$B/generated/include -I$B/_deps/nlohmann_json-src/include \
  $R/results/p12-mesh-007/tools/${1:-diag_g63}.cpp $B/src/libcfdcore.a -o $W/${1:-diag_g63} || exit 1
{
  echo "# P12-MESH-007 diagnosis ${1:-diag_g63} (evidence only); $(date -u +%Y-%m-%dT%H:%M:%SZ); lib $(sha256sum $B/src/libcfdcore.a | cut -c1-16)"
  $W/${1:-diag_g63}
} > $R/results/p12-mesh-007/logs/${2:-13_diag_g63_step1.log} 2>&1
cat $R/results/p12-mesh-007/logs/${2:-13_diag_g63_step1.log} | head -80

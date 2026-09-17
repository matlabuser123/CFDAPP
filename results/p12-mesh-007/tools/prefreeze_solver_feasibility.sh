#!/usr/bin/env bash
# P12-MESH-007 pre-freeze feasibility check of the gate's linear-solver settings on the EXISTING static
# PISO (build/release library, no MESH-007 code). Not a gate result.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
W=$HOME/m7pre; mkdir -p $W
c++ -std=c++20 -O3 -DNDEBUG -I$R/include -I$B/generated/include -I$B/_deps/nlohmann_json-src/include \
  $R/results/p12-mesh-007/tools/prefreeze_solver_feasibility.cpp $B/src/libcfdcore.a -o $W/feasibility || exit 1
{
  echo "# P12-MESH-007 pre-freeze solver-settings feasibility (existing static PISO; NOT a gate result); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# library: build/release/src/libcfdcore.a sha256 $(sha256sum $B/src/libcfdcore.a | cut -c1-16)"
  $W/feasibility
  echo "exit $?"
} > $R/results/p12-mesh-007/logs/04_prefreeze_solver_feasibility_NOT_gate.log 2>&1
cat $R/results/p12-mesh-007/logs/04_prefreeze_solver_feasibility_NOT_gate.log

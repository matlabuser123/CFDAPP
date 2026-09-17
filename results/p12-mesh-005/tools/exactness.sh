#!/usr/bin/env bash
# P12-MESH-005 §24a: compile tools/exactness_probe.cpp against the final Release library and record
# the measured worst-case deviations behind the pass/fail unit tests (gate sections A, C, D).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
L=$R/results/p12-mesh-005/logs/03_exactness_measurements.log
mkdir -p $HOME/m5probe
c++ -std=c++20 -O3 -DNDEBUG -I$R/include -I$B/generated/include -I$B/_deps/nlohmann_json-src/include \
  $R/results/p12-mesh-005/tools/exactness_probe.cpp $B/src/libcfdcore.a -o $HOME/m5probe/exactness_probe || exit 1
{
  echo "# P12-MESH-005 §24a measured exactness (production API; Release -O3 library build/release/src/libcfdcore.a)"
  echo "# probe: results/p12-mesh-005/tools/exactness_probe.cpp; lib sha256 $(sha256sum $B/src/libcfdcore.a | cut -c1-16); $(date -u)"
  $HOME/m5probe/exactness_probe
  echo "exit $?"
} > $L 2>&1
cat $L

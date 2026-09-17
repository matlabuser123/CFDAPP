#!/usr/bin/env bash
# P12-DIFF-002 W5 attribution: build diff2_w5_isolate.cpp twice from ONE source -- once against the
# real library (the DIFF-002 boundary treatment) and once with -DPRECHANGE, which makes the probe
# define the three symbols of src/discretization/NonOrthogonalDiffusion.cpp itself with their
# pre-DIFF-002 bodies, so the linker never pulls that archive member and the entire library above it
# runs against the pre-change boundary treatment. No production source is modified.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
W=$HOME/m7d2w5; mkdir -p $W
INC="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
LIB=$R/build/release/src/libcfdcore.a
c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/diff2_w5_isolate.cpp $LIB -o $W/after || exit 1
c++ -std=c++20 -O3 -DNDEBUG -DPRECHANGE $INC $P/tools/diff2_w5_isolate.cpp $LIB -o $W/before || exit 1
cd $R
{
  echo "# P12-DIFF-002 W5 attribution (libcfdcore.a sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# probe sha256 $(sha256sum $P/tools/diff2_w5_isolate.cpp | cut -c1-16)"
  echo
  $W/before
  echo
  $W/after
  echo "exit $?"
} > $P/logs/13_W5_attribution.log 2>&1
cat $P/logs/13_W5_attribution.log

#!/usr/bin/env bash
# P12-MESH-007: build the library and the MESH-007 test targets (build/release by default); print errors
# and warnings only.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=${1:-build/release}
cd $R
cmake --build $BUILD -j16 --target cfdcore CFDMeshTests CFDDiscretizationTests CFDPisoTests > $HOME/m7_build.log 2>&1
rc=$?
grep -E "error|warning:" $HOME/m7_build.log | head -60
echo "build rc=$rc; warnings $(grep -c 'warning:' $HOME/m7_build.log); errors $(grep -c 'error' $HOME/m7_build.log)"

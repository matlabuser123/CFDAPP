#!/bin/bash
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD032_lagged_time_level
echo "date $(date -Is)"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
sha256sum "$BUILD/src/libcfdcore.a"
echo
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  "$ITEM/tools/why_noflame_probe.cpp" -o "$ITEM/tools/td032_why" "$BUILD/src/libcfdcore.a"
if [ $? -ne 0 ]; then echo "*** ABORT: probe did not compile."; exit 1; fi
echo "compile exit: 0"
echo
"$ITEM/tools/td032_why"
echo "probe exit: $?"
echo "DONE $(date -Is)"

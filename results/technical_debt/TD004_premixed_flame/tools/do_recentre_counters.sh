#!/bin/bash
# TD-004 AUDIT -- the recentring operator's own decision counters. THE WORK ITSELF.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo
echo "=== PROVENANCE"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
echo "  (expect 8c85bc9f... = TD-027 cap 5 + TD-032 time level + TD-037 drift window)"
sha256sum "$ITEM/tools/recentre_counters_probe.cpp"
echo
echo "=== COMPILE"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/recentre_counters_probe.cpp" \
  -o "$ITEM/tools/td004_recentre" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
echo "compile exit: 0"
echo
echo "=== RUN"
"$ITEM/tools/td004_recentre"
echo "probe exit: $?"
echo "DONE $(date -Is)"

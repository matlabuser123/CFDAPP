#!/bin/bash
# TD-004 AUDIT -- the raw located-position trace. THE WORK ITSELF.
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
echo "  (expect 8c85bc9f... -- NO production change was made for this trace)"
sha256sum "$ITEM/tools/locator_trace_probe.cpp"
echo
echo "=== COMPILE"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/locator_trace_probe.cpp" \
  -o "$ITEM/tools/td004_trace" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
echo "compile exit: 0"
echo
echo "=== RUN  (8 solves; the search dominates each, so expect 40-60 min)"
"$ITEM/tools/td004_trace"
echo "probe exit: $?"
echo "DONE $(date -Is)"

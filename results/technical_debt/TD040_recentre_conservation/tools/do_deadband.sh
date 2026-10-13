#!/bin/bash
# TD-040 -- the dead-band ladder. THE WORK ITSELF.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD040_recentre_conservation
echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo
echo "=== PROVENANCE"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
echo "  (expect 3c2263cd... = with TD-004's locator fix; NO production change for this ladder)"
sha256sum "$ITEM/tools/deadband_ladder_probe.cpp"
echo
echo "=== COMPILE"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/deadband_ladder_probe.cpp" \
  -o "$ITEM/tools/td040_deadband" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
echo "compile exit: 0"
echo
echo "=== RUN  (5 solves)"
"$ITEM/tools/td040_deadband"
echo "probe exit: $?"
echo "DONE $(date -Is)"

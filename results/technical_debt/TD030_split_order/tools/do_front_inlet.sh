#!/bin/bash
# TD-030 AUDIT -- front position vs inlet fuel deficit. THE WORK ITSELF.
#
# This is a standalone script rather than a shell function passed through `bash -c`: the first
# attempt used `bash -c "$(declare -f run); run"`, which carries the function text into the new
# shell but NOT the variables it closes over, so $BUILD was empty and it aborted on a missing
# CMakeCache before measuring anything. A script file has no such capture problem.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD030_split_order

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "nproc       $(nproc)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo

echo "=== PROVENANCE"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1
if [ $? -ne 0 ]; then
  echo "*** ABORT: the library did not build."
  exit 1
fi
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
echo "  (expect 8c85bc9f... = TD-027 cap 5 + TD-032 time level + TD-037 drift window)"
sha256sum "$ITEM/tools/front_inlet_probe.cpp"
echo

echo "=== COMPILE"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/front_inlet_probe.cpp" \
  -o "$ITEM/tools/td030_front" "$BUILD/src/libcfdcore.a"
if [ $? -ne 0 ]; then
  echo "*** ABORT: the probe did not compile."
  exit 1
fi
echo "compile exit: 0"
echo

echo "=== RUN"
"$ITEM/tools/td030_front"
echo "probe exit: $?"
echo "DONE $(date -Is)"

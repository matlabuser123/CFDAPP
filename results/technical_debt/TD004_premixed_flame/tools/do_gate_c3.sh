#!/bin/bash
# TD-004 -- run the FROZEN gate from C3. Stops at the first failure, by the probe's own structure.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo
echo "=== PROVENANCE -- the gate and the tree"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
sha256sum "$BUILD/src/libcfdcore.a"
SRC=$(sha256sum "$REPO/src/species/PremixedFlame.cpp" | cut -d' ' -f1)
echo "$SRC  PremixedFlame.cpp"
if [ "$SRC" != "3c2263cd34f3ae93e7605fa56b4c7823f077b567c5ed9cc8ebd3b95da30204a9" ]; then
  echo "*** ABORT: the tree is NOT the post-locator-fix form. Expected 3c2263cd..."
  exit 1
fi
echo "  confirmed: post-locator-fix"
G=$(sha256sum "$ITEM/GATE.md" | cut -d' ' -f1)
echo "GATE.md  $G"
if [ "$G" != "12ccc5dd" ] && [ -n "$G" ]; then
  echo "  (GATE.md's frozen hash is recorded in logs/00_freeze.log; compared there, not here)"
fi
grep -E "GATE.md" "$ITEM/logs/00_freeze.log" | head -2
echo
echo "=== COMPILE"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/gate_c3_to_c6b_probe.cpp" \
  -o "$ITEM/tools/td004_gate_c3" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
echo "compile exit: 0"
echo
echo "=== RUN  (3 solves; relaxT 90 is ~9x the relaxT 10 march, expect 1.5-2 h)"
"$ITEM/tools/td004_gate_c3"
echo "probe exit: $?"
echo "DONE $(date -Is)"

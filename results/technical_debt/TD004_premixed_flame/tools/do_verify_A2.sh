#!/bin/bash
# TD-004 -- VERIFY the locator fix against the ALREADY-FROZEN criterion C2-A1.
#
# No new gate is written: GATE_A1.md (e57bc758...) is frozen and c2a1_probe.cpp (83a6c5d8...) is
# the instrument that measured both the original FAIL and the re-measurement. Both are re-checked
# against their frozen hashes before anything runs.
#
# Stage 1 re-runs the TRACE first, because it is the cheap decisive check: if previousLocated is
# now translated with the field, the locator must keep tracking past the first recentring and the
# front must stay near the anchor. If that fails there is no point spending 90 minutes on C2-A1.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "build type  $(grep -m1 '^CMAKE_BUILD_TYPE' "$BUILD/CMakeCache.txt")"
echo
echo "=== STAGE A  rebuild, prove the library CHANGED, prove the instruments did NOT"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
NEW=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "library now      $NEW"
echo "library before   be2520b7fb56293175112ca8726d7b53f2f3c23e03309c7f832b4292165717e5"
if [ "$NEW" = "be2520b7fb56293175112ca8726d7b53f2f3c23e03309c7f832b4292165717e5" ]; then
  echo "*** ABORT: the library did NOT change. Stale-binary results are not evidence."
  exit 1
fi
sha256sum "$REPO/src/species/PremixedFlame.cpp"
echo "  (before the fix: 8c85bc9f5f457bf22e1c19143db35a9e28db9ae957a1139f503d88da5e6eae2a)"
G=$(sha256sum "$ITEM/GATE_A1.md" | cut -d' ' -f1)
P=$(sha256sum "$ITEM/tools/c2a1_probe.cpp" | cut -d' ' -f1)
echo "GATE_A1.md       $G"
echo "c2a1_probe.cpp   $P"
if [ "$G" != "e57bc758ca5610eb7431438537de3e3211a884a9ed8a33adcefe0d3694eed42e" ] \
   || [ "$P" != "83a6c5d843466ba98770050dfeb942e0bdda6365193757f41bdb4a6fb63a89e7" ]; then
  echo "*** ABORT: a frozen instrument changed. The comparison would be invalid."
  exit 1
fi
echo "both match their frozen hashes"
echo

echo "=== STAGE B  the TRACE -- does the locator keep tracking past the first recentring?"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/locator_trace_probe.cpp" \
  -o "$ITEM/tools/td004_trace_fixed" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
"$ITEM/tools/td004_trace_fixed"
echo
echo "READ: finalRc should now grow past 1, rej>50 should STOP growing, and offCells should stay"
echo "      small. Before the fix: finalRc 1, rej>50 +1 per step, offCells -51.3 at 17194."
echo

echo "=== STAGE C  the FROZEN criterion C2-A1"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/c2a1_probe.cpp" \
  -o "$ITEM/tools/td004_c2a1_fixed" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
"$ITEM/tools/td004_c2a1_fixed"
echo "c2a1 exit: $?"
echo "DONE $(date -Is)"

#!/bin/bash
# TD-032 -- the same per-trial record on the PRE-FIX library, to close the attribution.
#
# Post-fix, trial 3 (mdot 0.0870611) extinguishes after 40 steps, so a 64-step drift window cannot
# read a drift and the trial gets no side. Pre-fix, window 64 CONVERGED -- so either that trial
# survived longer there, or it did not extinguish. This measures which, instead of inferring it.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD032_lagged_time_level
SWAP=$ITEM/tools/swap_td032.py
cd "$REPO" || exit 1
restore() {
  echo
  echo "=== ENSURING the tree is left FIXED"
  CUR=$(sha256sum "$REPO/src/species/PremixedFlame.cpp" | cut -d' ' -f1)
  if [ "$CUR" = "f9f554bf9e1fe1fd29f6ce4c185e7d579fa5c5708958112bc83127e4a6717b6e" ]; then
    echo "already fixed: $CUR"
  else
    python3 "$SWAP" in "$REPO" || echo "*** RESTORE FAILED -- CHECK THE TREE"
    cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1
    echo "restored: $(sha256sum "$REPO/src/species/PremixedFlame.cpp" | cut -c1-16)..."
  fi
}
trap restore EXIT
echo "date $(date -Is)"
python3 "$SWAP" out "$REPO" || { echo "*** ABORT: hash proof failed."; exit 1; }
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build."; exit 1; }
echo "pre-fix library $(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)"
echo
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  "$ITEM/tools/why_noflame_probe.cpp" -o "$ITEM/tools/td032_why_pre" "$BUILD/src/libcfdcore.a" \
  || { echo "*** ABORT: compile."; exit 1; }
"$ITEM/tools/td032_why_pre"
echo "probe exit: $?"
echo "DONE $(date -Is)"

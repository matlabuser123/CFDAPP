#!/bin/bash
# TD-032 -- run the discriminating probe on BOTH libraries.
#
# The question is about both trees, so the probe runs twice: the post-fix library (does NoFlame tell
# the truth?) and the reconstructed pre-fix library (what did its PASS actually converge on?). Both
# source forms are hash-proved by swap_td032.py, which refuses to write a reconstruction whose
# sha256 does not match the frozen value.
#
# Window 32 is the internal control -- it passes on both libraries -- so it shows what a "passing"
# result looks like on each, which is what makes the window-64 comparison interpretable.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD032_lagged_time_level
SWAP=$ITEM/tools/swap_td032.py
SRC=$ITEM/tools/window64_probe.cpp

cd "$REPO" || exit 1

restore() {
  echo
  echo "=== ENSURING the tree is left in the FIXED form"
  CUR=$(sha256sum "$REPO/src/species/PremixedFlame.cpp" | cut -d' ' -f1)
  if [ "$CUR" = "f9f554bf9e1fe1fd29f6ce4c185e7d579fa5c5708958112bc83127e4a6717b6e" ]; then
    echo "already fixed: $CUR"
  else
    echo "tree is NOT fixed ($CUR) -- restoring"
    python3 "$SWAP" in "$REPO" || echo "*** RESTORE FAILED -- CHECK THE TREE MANUALLY"
    cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1
  fi
}
trap restore EXIT

build_and_run() {
  local label=$1 out=$2
  cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1
  if [ $? -ne 0 ]; then
    echo "*** ABORT: $label library did not build."
    exit 1
  fi
  echo "$label library  $(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)"
  /usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
    -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
    -Wall -Wextra -Wpedantic "$SRC" -o "$out" "$BUILD/src/libcfdcore.a"
  if [ $? -ne 0 ]; then
    echo "*** ABORT: the probe did not compile against the $label library."
    exit 1
  fi
  echo
  "$out"
  echo "probe exit: $?"
}

echo "TD-032 -- DISCRIMINATING PROBE, both libraries"
echo "date $(date -Is)"
echo

echo "############################################################ POST-FIX (the current tree)"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
build_and_run "post-fix" "$ITEM/tools/td032_w64_post"
echo

echo "############################################################ PRE-FIX (reconstructed)"
python3 "$SWAP" out "$REPO"
if [ $? -ne 0 ]; then
  echo "*** ABORT: the reconstruction failed its hash proof."
  exit 1
fi
build_and_run "pre-fix" "$ITEM/tools/td032_w64_pre"
echo

echo "DONE $(date -Is)"

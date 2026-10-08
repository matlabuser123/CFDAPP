#!/bin/bash
# TD-004 -- the recentring ledger on BOTH libraries. Two-library per CLAUDE.md section 6.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
SWAP=$ITEM/tools/swap_td004.py

restore() {
  echo
  echo "=== ENSURING the tree is left in the POST-FIX form"
  CUR=$(sha256sum "$REPO/src/species/PremixedFlame.cpp" | cut -d' ' -f1)
  if [ "$CUR" = "3c2263cd34f3ae93e7605fa56b4c7823f077b567c5ed9cc8ebd3b95da30204a9" ]; then
    echo "already post-fix: $CUR"
  else
    echo "tree is NOT post-fix ($CUR) -- restoring"
    python3 "$SWAP" in "$REPO" || echo "*** RESTORE FAILED -- CHECK THE TREE MANUALLY"
    cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1
  fi
}
trap restore EXIT

build_run() {
  local label=$1 out=$2
  cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
  echo "$label library  $(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)"
  /usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
    -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
    -Wall -Wextra -Wpedantic "$ITEM/tools/ledger_probe_A2.cpp" -o "$out" \
    "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
  echo
  "$out"
}

echo "date $(date -Is)"
echo
echo "############################ POST-FIX (locator translated)"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
build_run "post-fix" "$ITEM/tools/td004_ledger_post"
echo
echo "############################ PRE-FIX (locator NOT translated, reconstructed)"
python3 "$SWAP" out "$REPO" || { echo "*** ABORT: the reconstruction failed its hash proof."; exit 1; }
build_run "pre-fix" "$ITEM/tools/td004_ledger_pre"
echo
echo "DONE $(date -Is)"

#!/bin/bash
# TD-040 -- THE DECISIVE CONTROL: the SAME dead-band ladder on the PRE-FIX library.
#
# THE LOGIC. The post-fix ladder gave S_L = 4.955332 at every dead band, including with recentring
# OFF -- so translation does not set the eigenvalue. By elimination the +6.07 % came from the
# LOCATOR, via the trials' drift measurement: `history` is appended only when `locateFront`
# succeeds, and pre-fix the first recentring inside each trial blinded it.
#
# If that is right, then on the PRE-FIX library a dead band large enough that recentring NEVER
# FIRES leaves the locator unblinded -- and a blind-locator build should then produce the SAME
# eigenvalue as the fixed one.
#
# PRE-REGISTERED READINGS:
#   pre-fix deadBand 3    -> 4.671937   validates that this IS the pre-fix library and the probe
#                                        is comparable. Any other value voids the whole run.
#   pre-fix deadBand 1000 -> 4.955332   recentring-induced BLINDING was the cause. The correct
#                                        eigenvalue is 4.955332 and TD-040 drops to a reporting
#                                        defect rather than a P0 physics one.
#   pre-fix deadBand 1000 -> 4.671937   blinding is NOT the cause; the libraries differ some other
#                                        way and the elimination argument is wrong.
#   pre-fix deadBand 1000 -> neither    a third mechanism; stop and characterise it.
#
# The probe source is UNCHANGED -- only the library it links differs, which is the two-library
# comparison CLAUDE.md section 6 requires.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD040_recentre_conservation
SWAP=$REPO/results/technical_debt/TD004_premixed_flame/tools/swap_td004.py

restore() {
  echo
  echo "=== ENSURING the tree is left POST-FIX"
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

echo "date $(date -Is)"
echo
echo "=== reconstruct the PRE-FIX source, hash-proved"
python3 "$SWAP" out "$REPO" || { echo "*** ABORT: the reconstruction failed its hash proof."; exit 1; }
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
PRE=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "pre-fix library  $PRE"
echo "  (TD-004's A2_05 measured the pre-fix library as be2520b7fb56293175112ca8726d7b53f2f3c23e03309c7f832b4292165717e5)"
echo
echo "=== the probe source must be UNCHANGED from the post-fix run"
sha256sum "$ITEM/tools/deadband_ladder_probe.cpp"
echo
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/deadband_ladder_probe.cpp" \
  -o "$ITEM/tools/td040_deadband_pre" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
echo "=== RUN on the PRE-FIX library"
"$ITEM/tools/td040_deadband_pre"
echo "probe exit: $?"
echo "DONE $(date -Is)"

#!/bin/bash
# TD-004 GATE_A3 -- M5, the control C5b's DERIVATION implies, and a test OF that derivation.
#
# WHY THIS LEG EXISTS. Dry-run pass 2 was clean (every library assertion passed, REF reproduced
# A2_07 exactly) but a PRE-REGISTERED STOP fired: C5b never failed.
#   M2  deadBand 1, post-fix: |min Y| 1.184e-11 against deadBand 3's 1.890e-11 -- SMALLER. So
#       "min Y_i tracks the recentring fill count" is REFUTED on valid legs.
#   M4  species update x 1.0001: |min Y| 1.185e-11, PASS. It failed to fail.
#       And it is now clear WHY, which is the more useful finding: M4 made C5a fail at
#       1.510976e-01 K, within 5e-05 of M3's 1.510480e-01. Consuming fuel 1.0001x faster while
#       adding heat at 1.0000x reaches full depletion after 1/1.0001 of the heat, giving
#       T_u + 1500/1.0001 = 1799.85 K -- the SAME arithmetic as scaling heat by 0.9999. M4 and M3
#       are two expressions of one corruption, so M4 was never an independent control at all.
#
# WHAT M5 DOES DIFFERENTLY. C5b's bound is `residualAcceptanceFraction * Y_F,u`, and
# PremixedFlame.cpp:662 holds that fraction as a hardcoded `1.0e-9`. Raising it to `1.0e-6` varies
# THE EXACT QUANTITY THE THRESHOLD IS DERIVED FROM. This should have been the first choice; M2 and
# M4 were chosen from a mechanism (fill count) that the data has now refuted.
#
# PRE-REGISTERED, one reading per direction, no prediction between them:
#   |min Y| ~ 1.9e-08 (about 1000x)  -> min Y IS linear-acceptance-limited, exactly as C5b was
#                                       derived. C5b gains its negative control (fails by ~378x)
#                                       AND its derivation is corroborated. GATE_A3 can be frozen.
#   |min Y| ~ 1e-11 (unmoved)        -> THE DERIVATION IS WRONG. C5b is not acceptance-limited, it
#                                       has no control, and it must be RE-DERIVED from whatever
#                                       does govern it. GATE_A3 is NOT frozen and the amendment
#                                       stops at C5b, which is what GATE_A3.md §5 requires.
#   |min Y| grows but far less than 1000x -> partially acceptance-limited; the bound's FORM is
#                                       right but its coefficient is not, and it must be re-derived
#                                       before freezing.
#
# A SECOND, UNPLANNED FINDING from pass 2 that this leg also bears on: M2 made C5d FAIL
# (|sum Y - 1| = 1.235e-06 at deadBand 1 against 8.823e-07 at deadBand 3). So composition-sum
# degradation DOES track the recentring count even though min Y does not -- the operator damages
# conservation through the SUM, not through negativity. C5d therefore has a negative control it was
# not designed to have, and the finding belongs to TD-040.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
SRC=$REPO/src/species/PremixedFlame.cpp
BACKUP=$ITEM/tools/.PremixedFlame.cpp.m5backup
OUT=$ITEM/logs/A3_03_m5_acceptance.log
PROBE=$ITEM/tools/c5_amended_probe.cpp
BIN=$ITEM/tools/td004_c5m5
POST_SRC=fa815c9721658448
PRE_LIB=1eb0e2cb8b1083ec56d7c5681dbc7cafcd6feb78910e2dce8db3c9e516113c68
POST_LIB=852555cfda54f8da013bde93f9c205c15daf2e5291d7920a3744b86e9853c7f8

if [ -e "$OUT" ]; then echo "*** ABORT: $OUT exists."; exit 1; fi
LOAD=$(awk '{print int($1)}' /proc/loadavg)
if [ "$LOAD" -gt 6 ]; then echo "*** ABORT: load $LOAD"; exit 1; fi
CUR=$(sha256sum "$SRC" | cut -c1-16)
if [ "$CUR" != "$POST_SRC" ]; then echo "*** ABORT: want $POST_SRC got $CUR"; exit 1; fi
cp "$SRC" "$BACKUP" || { echo "*** ABORT: backup"; exit 1; }

restore() {
  echo
  echo "=== EXIT TRAP"
  if [ -e "$BACKUP" ]; then
    cp "$BACKUP" "$SRC"; touch "$SRC"
    R=$(sha256sum "$SRC" | cut -c1-16)
    if [ "$R" = "$POST_SRC" ]; then
      echo "post-fix source restored and hash-verified"
      rm -f "$BACKUP"
      cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 \
        && echo "library rebuilt: $(sha256sum "$BUILD/src/libcfdcore.a" | cut -c1-32)" \
        || echo "*** rebuild FAILED -- rebuild cfdcore manually"
    else
      echo "*** RESTORE MISMATCH got $R -- BACKUP KEPT at $BACKUP"
    fi
  fi
}
trap restore EXIT

exec > >(tee "$OUT") 2>&1

echo "=== ENVIRONMENT"
echo "date   $(date -Is)"
echo "nproc  $(nproc)   load $(cat /proc/loadavg)"
echo
echo "=== PROVENANCE"
sha256sum "$SRC" "$PROBE" "$ITEM/GATE_A3.md"
echo
echo "=== the constant being varied, BEFORE the patch"
grep -n "residualAcceptanceFraction = " "$SRC"
echo

echo "############################################################ M5  acceptance 1e-9 -> 1e-6"
python3 "$ITEM/tools/a3_patch.py" acceptance "$SRC" || { echo "*** ABORT: M5 patch"; exit 1; }
grep -n "residualAcceptanceFraction = " "$SRC"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
LIB=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "library  $LIB"
if [ "$LIB" = "$PRE_LIB" ] || [ "$LIB" = "$POST_LIB" ]; then
  echo "*** ABORT: the patched library equals an unpatched one -- the patch did not reach the binary."
  exit 1
fi
echo "  OK: differs from both uncorrupted libraries."
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$PROBE" -o "$BIN" "$BUILD/src/libcfdcore.a" \
  || { echo "*** ABORT: compile"; exit 1; }
"$BIN" 3.0 M5_acceptance1e6
echo

echo "############################################################ HOW TO READ THIS"
echo "  REF (post-fix, acceptance 1e-9) measured |min Y| = 1.889798e-11."
echo "  If min Y is acceptance-limited, 1000x the acceptance gives ~1.9e-08 and C5b FAILS by ~378x."
echo "  If it is unmoved, C5b's DERIVATION is wrong -- not merely its control -- and GATE_A3.md"
echo "  may NOT be frozen. Either way the verdict goes in dry_run_A3.md, not here."
echo
echo "DONE $(date -Is)"

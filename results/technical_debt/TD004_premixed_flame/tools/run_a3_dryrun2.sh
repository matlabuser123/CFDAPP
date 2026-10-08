#!/bin/bash
# TD-004 GATE_A3 DRY-RUN, PASS 2. Pass 1 is retained as run_a3_dryrun.sh and its log is marked
# INVALID AS AUTHORITATIVE; this file exists rather than patching it so the instrument defect stays
# on the record.
#
# WHAT PASS 1 GOT WRONG, and it was the harness, not the gate:
#   It restored the post-fix source with `cp -p`, which PRESERVES THE BACKUP'S MTIME. That mtime is
#   older than the object already built from the patched source, so ninja rebuilt nothing and the
#   REF and M2 legs silently ran the PRE-FIX library. Both reported pre-fix numbers to the digit.
#   Pass 1 PRINTED the library hash and asserted NOTHING about it -- it failed OPEN. What caught it
#   was comparing REF against A2_07's recorded post-fix values, i.e. a reproduction control.
#
# THE TWO FIXES:
#   1  restore with `cp` (no -p) and then `touch`, so the source is unambiguously newer than any
#      object built from it;
#   2  assert the library hash per leg, against the values A2_07 recorded. A leg whose library is
#      not what it claims ABORTS. CLAUDE.md section 8: a result is authoritative only if the binary
#      was demonstrably rebuilt against the exact source being tested.
#
# AND ONE DESIGN CHANGE, because pass 1 reported a pre-registered stop:
#   C5b NEVER FAILED on any leg. GATE_A3.md section 3.2 registered that if M2 (deadBand 1) left
#   |min Y_i| near 1e-11 then the "min Y_i tracks the fill count" mechanism is WRONG and C5b has no
#   control. The invalid M2 pointed the OPPOSITE way from the mechanism -- deadBand 1 gave 6.20e-12,
#   SMALLER than deadBand 3's 8.45e-12. M2 is therefore re-cast as a MECHANISM TEST, informative
#   either way, and a corrupted-operator control M4 is added as C5b's actual negative control.
#   Choosing an instrument empirically is legitimate; what must never be fitted to observation is a
#   THRESHOLD, and no threshold changes here.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
SRC=$REPO/src/species/PremixedFlame.cpp
BACKUP=$ITEM/tools/.PremixedFlame.cpp.a3backup2
OUT=$ITEM/logs/A3_02_dryrun.log
PROBE=$ITEM/tools/c5_amended_probe.cpp
BIN=$ITEM/tools/td004_c5a3
POST_SRC=fa815c9721658448
PRE_LIB=1eb0e2cb8b1083ec56d7c5681dbc7cafcd6feb78910e2dce8db3c9e516113c68
POST_LIB=852555cfda54f8da013bde93f9c205c15daf2e5291d7920a3744b86e9853c7f8

if [ -e "$OUT" ]; then echo "*** ABORT: $OUT exists."; exit 1; fi
LOAD=$(awk '{print int($1)}' /proc/loadavg)
if [ "$LOAD" -gt 6 ]; then echo "*** ABORT: load $LOAD"; exit 1; fi
CUR=$(sha256sum "$SRC" | cut -c1-16)
if [ "$CUR" != "$POST_SRC" ]; then echo "*** ABORT: want $POST_SRC got $CUR"; exit 1; fi
cp "$SRC" "$BACKUP" || { echo "*** ABORT: backup failed"; exit 1; }

restore_source() {
  cp "$BACKUP" "$SRC"
  touch "$SRC"
  local r
  r=$(sha256sum "$SRC" | cut -c1-16)
  [ "$r" = "$POST_SRC" ] || { echo "*** RESTORE MISMATCH: got $r"; return 1; }
  return 0
}

restore() {
  echo
  echo "=== EXIT TRAP"
  if [ -e "$BACKUP" ]; then
    if restore_source; then
      echo "post-fix source restored and hash-verified"
      rm -f "$BACKUP"
      cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 \
        && echo "library rebuilt: $(sha256sum "$BUILD/src/libcfdcore.a" | cut -c1-32)" \
        || echo "*** rebuild FAILED -- rebuild cfdcore manually"
    else
      echo "*** BACKUP KEPT at $BACKUP"
    fi
  else
    echo "*** NO BACKUP -- inspect $SRC"
  fi
}
trap restore EXIT

exec > >(tee "$OUT") 2>&1

echo "=== ENVIRONMENT"
echo "date   $(date -Is)"
echo "nproc  $(nproc)   load $(cat /proc/loadavg)"
grep CMAKE_BUILD_TYPE "$BUILD/CMakeCache.txt"
echo
echo "=== PROVENANCE"
sha256sum "$SRC" "$PROBE" "$ITEM/GATE.md" "$ITEM/GATE_A3.md"
echo "  GATE_A3.md is NOT frozen; this dry-run is what qualifies it for freezing."
echo
echo "=== EXPECTED LIBRARY HASHES, from A2_07 -- asserted, not merely printed"
echo "  pre-fix   $PRE_LIB"
echo "  post-fix  $POST_LIB"
echo

# build_and_run <label> <deadBand> <expect: a sha256, or DIFFERENT_FROM_BOTH>
build_and_run() {
  local label="$1" band="$2" expect="$3"
  cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 \
    || { echo "*** ABORT: build ($label)"; exit 1; }
  local lib
  lib=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
  echo "library  $lib"
  if [ "$expect" = "DIFFERENT_FROM_BOTH" ]; then
    if [ "$lib" = "$PRE_LIB" ] || [ "$lib" = "$POST_LIB" ]; then
      echo "*** ABORT ($label): the corrupted library equals an UNCORRUPTED one."
      echo "    The patch did not reach the binary. This is pass 1's defect and it is fatal."
      exit 1
    fi
    echo "  OK: differs from both uncorrupted libraries, so the corruption reached the binary."
  else
    if [ "$lib" != "$expect" ]; then
      echo "*** ABORT ($label): WRONG LIBRARY."
      echo "    want $expect"
      echo "    got  $lib"
      echo "    A leg that cannot prove which library it ran is not evidence (CLAUDE.md section 8)."
      exit 1
    fi
    echo "  OK: matches the expected library for this leg."
  fi
  /usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
    -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
    -Wall -Wextra -Wpedantic "$PROBE" -o "$BIN" "$BUILD/src/libcfdcore.a" \
    || { echo "*** ABORT: compile ($label)"; exit 1; }
  "$BIN" "$band" "$label"
}

patch_locator_out() {
  python3 "$ITEM/tools/a3_patch.py" locator_out "$SRC"
}
patch_heat() {
  python3 "$ITEM/tools/a3_patch.py" heat "$SRC"
}
patch_species() {
  python3 "$ITEM/tools/a3_patch.py" species "$SRC"
}

echo "############################################################ M1  pre-fix, deadBand 3"
patch_locator_out || { echo "*** ABORT: M1 patch"; exit 1; }
build_and_run M1_prefix 3.0 "$PRE_LIB"
echo

echo "############################################################ REF  post-fix, deadBand 3"
restore_source || { echo "*** ABORT: restore before REF"; exit 1; }
build_and_run REF_postfix 3.0 "$POST_LIB"
echo

echo "############################################################ M2  post-fix, deadBand 1"
echo "  MECHANISM TEST, not C5b's control. Same library, only the setting differs."
echo "  Does |min Y_i| grow with the recentring count? Pass 1's invalid leg said NO."
"$BIN" 1.0 M2_deadband1
echo

echo "############################################################ M3  heat release x 0.9999"
restore_source || { echo "*** ABORT: restore before M3"; exit 1; }
patch_heat || { echo "*** ABORT: M3 patch"; exit 1; }
build_and_run M3_heat9999 3.0 DIFFERENT_FROM_BOTH
echo

echo "############################################################ M4  species update x 1.0001"
echo "  C5b's negative control. Over-integrating the species update drives Y_fuel below zero at"
echo "  the trailing edge by ~1e-4 of its scale, i.e. ~5e-06 against a 5e-11 bound. Scaling ALL"
echo "  species by one factor keeps sum(sources) = 0, so this targets C5b WITHOUT corrupting C5d."
restore_source || { echo "*** ABORT: restore before M4"; exit 1; }
patch_species || { echo "*** ABORT: M4 patch"; exit 1; }
build_and_run M4_species10001 3.0 DIFFERENT_FROM_BOTH
echo

echo "############################################################ NON-VACUITY SUMMARY"
echo
grep "MACHINE-READABLE" "$OUT" | sed 's/^ *//'
echo
echo "REQUIRED for GATE_A3.md to be frozen:"
echo "  C5a must FAIL on at least one leg   (expected: M3)"
echo "  C5b must FAIL on at least one leg   (expected: M4)"
echo "  C5c must FAIL on at least one leg   (expected: M1)"
echo "  C5d must PASS on REF, and is carried over unchanged"
echo "  REF must reproduce A2_07: T_peak 1799.998974, min Y -1.889798e-11, front +3.499219 mm"
echo
echo "M2 is a MECHANISM TEST and has no required outcome. Either result is recorded:"
echo "  |min Y| grows with deadBand 1 -> the fill-count mechanism holds"
echo "  |min Y| does not grow         -> the mechanism is refuted and verification_A3.md section 4"
echo "                                   must say so. C5b's bound then rests on its DERIVATION"
echo "                                   from the linear acceptance fraction alone, with M4 as its"
echo "                                   only control -- which is sufficient for non-vacuity but"
echo "                                   means the mechanism claim must be withdrawn."
echo
echo "The verdict is written in dry_run_A3.md, not asserted here."
echo "DONE $(date -Is)"

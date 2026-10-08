#!/bin/bash
# TD-004 GATE_A3 -- THE POST-FREEZE VERIFICATION.
#
# CLAUDE.md §5.3 step 6: "Rerun fresh. Reuse no earlier result." The dry-run's REF leg measured this
# same configuration and passed all four clauses, and it is deliberately NOT reused here.
#
# THREE FAIL-CLOSED GATES BEFORE ANY MEASUREMENT:
#   1  GATE_A3.md must still hash to the value frozen in logs/A3_00_freeze.log. A gate edited after
#      freezing is not a frozen gate, and the repo's frozen-hash check runs only at PHASE end, so a
#      post-freeze edit would otherwise hide for the whole phase. It is recomputed here, at the
#      start of the step that depends on it.
#   2  the source must be the post-fix tree, carrying NO control patch from the dry-run.
#   3  the library must be the uncorrupted post-fix archive.
#
# The criteria are then applied IN THE FROZEN ORDER C5a -> C5b -> C5c -> C5d, stopping at the first
# failure, as CLAUDE.md §5.3 requires. The probe reports all four; the STOP RULE is applied here.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
OUT=$ITEM/logs/A3_04_verification.log
PROBE=$ITEM/tools/c5_amended_probe.cpp
BIN=$ITEM/tools/td004_c5verify

FROZEN_GATE=8a11ad39a147830ce5e2d1e91310c85212a881222ba143bde177a2771e6fb113
POST_SRC=fa815c97216584485e1c94842669fbf71976b9fb06863096868972d88448e3a2
POST_LIB=852555cfda54f8da013bde93f9c205c15daf2e5291d7920a3744b86e9853c7f8

if [ -e "$OUT" ]; then echo "*** ABORT: $OUT exists."; exit 1; fi
LOAD=$(awk '{print int($1)}' /proc/loadavg)
if [ "$LOAD" -gt 6 ]; then echo "*** ABORT: load $LOAD"; exit 1; fi

exec > >(tee "$OUT") 2>&1

echo "=== ENVIRONMENT"
echo "date   $(date -Is)"
echo "host   $(uname -sr)"
echo "nproc  $(nproc)   load $(cat /proc/loadavg)"
grep CMAKE_BUILD_TYPE "$BUILD/CMakeCache.txt"
echo

echo "=== GATE 1: is the frozen amendment still the frozen amendment?"
G=$(sha256sum "$ITEM/GATE_A3.md" | cut -d' ' -f1)
echo "  GATE_A3.md  $G"
if [ "$G" != "$FROZEN_GATE" ]; then
  echo "*** ABORT: GATE_A3.md has CHANGED since it was frozen."
  echo "    frozen $FROZEN_GATE"
  echo "    now    $G"
  echo "    A gate edited after freezing is not a frozen gate. Stopping."
  exit 1
fi
echo "  OK: matches logs/A3_00_freeze.log."
echo

echo "=== GATE 2: is the tree the post-fix source, with no control patch left behind?"
S=$(sha256sum "$REPO/src/species/PremixedFlame.cpp" | cut -d' ' -f1)
echo "  PremixedFlame.cpp  $S"
if [ "$S" != "$POST_SRC" ]; then
  echo "*** ABORT: not the post-fix source. A dry-run control patch may still be in the tree."
  exit 1
fi
echo "  OK."
grep -n "residualAcceptanceFraction = " "$REPO/src/species/PremixedFlame.cpp" | sed 's/^/  /'
echo "  (must read 1.0e-9 -- M5 raised it to 1.0e-6 and the EXIT trap restored it)"
echo

echo "=== GATE 3: is the library the uncorrupted post-fix archive?"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
L=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "  libcfdcore.a  $L"
if [ "$L" != "$POST_LIB" ]; then
  echo "*** ABORT: wrong library."
  echo "    want $POST_LIB"
  echo "    got  $L"
  exit 1
fi
echo "  OK."
echo
sha256sum "$PROBE"
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$PROBE" -o "$BIN" "$BUILD/src/libcfdcore.a" \
  || { echo "*** ABORT: compile"; exit 1; }
echo

echo "############################################################ FRESH VERIFICATION RUN"
"$BIN" 3.0 A3_VERIFY | tee /tmp/a3_verify_run.txt
echo

echo "############################################################ THE STOP RULE, IN FROZEN ORDER"
echo
LINE=$(grep "MACHINE-READABLE" /tmp/a3_verify_run.txt | tail -1)
echo "  $LINE"
echo
STOPPED=""
for c in C5a C5b C5c C5d; do
  V=$(echo "$LINE" | grep -oE "$c=[A-Z]+" | cut -d= -f2)
  if [ -z "$STOPPED" ]; then
    echo "  $c  $V"
    if [ "$V" != "PASS" ]; then
      STOPPED=$c
      echo
      echo "  *** STOPPED AT $c. Nothing below it is evaluated, per CLAUDE.md §5.3."
    fi
  else
    echo "  $c  NOT EVALUATED (the gate stopped at $STOPPED)"
  fi
done
echo
if [ -z "$STOPPED" ]; then
  echo "  C5a..C5d ALL PASS on the frozen amendment."
  echo "  This advances the gate to C6. It does NOT close TD-004: C6, C6a and C7 have never been"
  echo "  run, and TD-040 carries the S_L +6.07 % stop independently of C5."
else
  echo "  C5 FAILS at $STOPPED on the frozen amendment. The amendment is NOT satisfied."
fi
echo
echo "DONE $(date -Is)"

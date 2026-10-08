#!/bin/bash
# TD-004 C5 -- the three failing clauses at FULL PRECISION, on BOTH libraries.
#
# WHY A NEW HARNESS RATHER THAN swap_td004.py. That script's expected hashes were invalidated by
# TD-034's clang-format reformat: it wants POST_FIX_SHA 3c2263cd, and the tree is now fa815c97.
# It FAILED CLOSED, which is correct behaviour and is why it is not simply re-pointed here.
#
# PASS 1 OF THIS HARNESS ABORTED, CORRECTLY, AND THE REASON CHANGED THE DESIGN.
# It required the rebuilt post-fix library to reproduce A2_05's archive hash 43a24883. It gave
# 852555cf instead. Diagnosed rather than assumed:
#   (B) build nondeterminism -- RULED OUT. Three rebuilds of untouched source: 852555cf each time.
#   (C) the tree changed     -- CONFIRMED. 22 headers and sources were reformatted at 23:43 on
#       2026-10-08, AFTER A2_06 finished at 23:34, PremixedFlame.hpp among them.
# libcfdcore.a is an ARCHIVE AGGREGATING all 22 reformatted objects, so its hash MUST change even
# though formatting cannot change semantics. The archive hash was never a valid instrument for
# "is the reformat inert", and my earlier claim that it proved inertness used the wrong one.
#
# WHAT THIS HARNESS PROVES INSTEAD:
#   (a) the reverse-apply removes EXACTLY ONE line, asserted, and that line is the locator fix;
#   (b) the two archives must DIFFER, or there is no two-library comparison at all;
#   (c) INERTNESS IS PROVED BY PHYSICS: the post-fix run must reproduce A2_06 -- steps 15283,
#       T_peak 1799.99897, min Y -1.8898e-11, S_L 4.95533. A reformatted tree returning A2_06's
#       numbers is semantically inert, which is the claim that matters and is stronger than
#       archive equality. The probe carries that control and the harness ABORTS on DIFFERS.
#
# src/species/PremixedFlame.cpp is UNTRACKED (the whole P18 species subsystem is), so `git checkout`
# CANNOT restore it. A byte copy is taken first and the EXIT trap restores from that, verifying the
# hash afterwards.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
SRC=$REPO/src/species/PremixedFlame.cpp
BACKUP=$ITEM/tools/.PremixedFlame.cpp.c5backup
OUT=$ITEM/logs/A2_07_c5_precision.log

POST_SRC=fa815c9721658448
# Retained as a RECORD of what A2_05 measured pre-reformat. No longer used as gates -- see the
# header: the archive aggregates 22 reformatted objects, so equality was the wrong instrument.
POST_LIB=43a2488314395ca5479c54adafd0aa2007e9aa80d029c7eb06713de07935e9db
PRE_LIB=be2520b7fb56293175112ca8726d7b53f2f3c23e03309c7f832b4292165717e5

# ---------------------------------------------------------------- fail-closed guards, before anything
if [ -e "$OUT" ]; then
  echo "*** ABORT: $OUT already exists. Refusing to overwrite a recorded result."
  exit 1
fi
LOAD=$(awk '{print int($1)}' /proc/loadavg)
if [ "$LOAD" -gt 6 ]; then
  echo "*** ABORT: load average $LOAD -- another authoritative run may hold the machine."
  exit 1
fi
CUR=$(sha256sum "$SRC" | cut -c1-16)
if [ "$CUR" != "$POST_SRC" ]; then
  echo "*** ABORT: the tree is not the post-fix reformatted source."
  echo "    want $POST_SRC, got $CUR"
  exit 1
fi
cp -p "$SRC" "$BACKUP" || { echo "*** ABORT: could not back up the source"; exit 1; }

restore() {
  echo
  echo "=== EXIT TRAP: restoring the post-fix source from the byte copy"
  if [ -e "$BACKUP" ]; then
    cp -p "$BACKUP" "$SRC"
    R=$(sha256sum "$SRC" | cut -c1-16)
    if [ "$R" = "$POST_SRC" ]; then
      echo "restored and hash-verified: $R"
      rm -f "$BACKUP"
      cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 \
        && echo "library rebuilt post-fix" || echo "*** rebuild FAILED -- rebuild cfdcore manually"
    else
      echo "*** RESTORE MISMATCH: got $R, want $POST_SRC. BACKUP KEPT at $BACKUP"
    fi
  else
    echo "*** NO BACKUP FOUND -- inspect $SRC manually"
  fi
}
trap restore EXIT

exec > >(tee "$OUT") 2>&1

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "host        $(uname -sr)"
echo "nproc       $(nproc)"
echo "load        $(cat /proc/loadavg)"
grep CMAKE_BUILD_TYPE "$BUILD/CMakeCache.txt"
echo
echo "=== PROVENANCE"
sha256sum "$SRC"
echo "  (post-fix, AFTER TD-034's reformat; pre-reformat it was 3c2263cd...)"
sha256sum "$ITEM/tools/c5_precision_probe.cpp"
sha256sum "$ITEM/GATE.md"
echo "  GATE.md frozen hash is in logs/00_freeze.log -- compared there, not here"
echo
echo "############################################################ POST-FIX"
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
LIB=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "library      $LIB"
echo "  A2_05 recorded 43a24883... on the PRE-REFORMAT tree. A difference here is EXPECTED and"
echo "  is NOT a defect: the archive aggregates the 22 objects TD-034's reformat touched."
echo "  Semantic inertness is proved below by reproducing A2_06's physics instead."
echo
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/c5_precision_probe.cpp" \
  -o "$ITEM/tools/td004_c5_post" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
"$ITEM/tools/td004_c5_post" | tee /tmp/c5_post_run.txt
if grep -q "DIFFERS" /tmp/c5_post_run.txt; then
  echo
  echo "*** ABORT: the post-fix run does NOT reproduce A2_06."
  echo "    The reformatted tree returns different physics, so TD-034's reformat was NOT inert"
  echo "    and every number in A2_06 needs re-measuring. That is a finding in its own right."
  echo "    Stopping before the pre-fix leg: comparing against an unreproduced post-fix is"
  echo "    worthless."
  exit 1
fi
echo "  REPRODUCTION CONTROL PASSED: the reformatted tree returns A2_06's numbers."
echo "  -> TD-034's reformat is SEMANTICALLY INERT, proved by physics rather than archive bytes."
echo

echo "############################################################ PRE-FIX (reverse-applied)"
python3 - "$SRC" <<'PYEOF'
import hashlib, io, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8", newline="").read()
needle = "    previousLocated -= static_cast<Real>(shift) * cellSize;\n"
if needle not in s:
    needle = needle.replace("\n", "\r\n")
n = s.count(needle)
if n != 1:
    sys.stderr.write("*** ABORT: the locator fix line appears %d times, expected exactly 1\n" % n)
    sys.exit(1)
out = s.replace(needle, "")
io.open(p, "w", encoding="utf-8", newline="").write(out)
print("removed exactly 1 line: the locator-fix statement inside recentre()")
print("pre-fix source sha256: %s" % hashlib.sha256(out.encode("utf-8")).hexdigest()[:16])
PYEOF
if [ $? -ne 0 ]; then echo "*** ABORT: reverse-apply failed"; exit 1; fi
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
LIB2=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "library      $LIB2"
if [ "$LIB2" = "$LIB" ]; then
  echo "*** ABORT: the library did NOT change after reverse-applying the fix."
  echo "    A two-library comparison with one library is not a comparison."
  exit 1
fi
echo "  (A2_05 recorded be2520b7... pre-reformat; not comparable, for the same reason.)"
echo "  The baseline here is CLAUDE.md section 6's FIRST admitted form: an isolated copy with"
echo "  ONLY the locator fix removed -- exactly one line, asserted above -- built by the same"
echo "  toolchain that just reproduced A2_06 exactly."
echo
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$ITEM/tools/c5_precision_probe.cpp" \
  -o "$ITEM/tools/td004_c5_pre" "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile"; exit 1; }
"$ITEM/tools/td004_c5_pre"
echo
echo "=== BOTH LIBRARIES MEASURED. The comparison is in verification_A3.md, not here."
echo "DONE $(date -Is)"

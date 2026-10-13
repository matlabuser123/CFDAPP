#!/bin/bash
# TD-004 GATE_A3 -- THE DRY-RUN AND NON-VACUITY CHECK, before the amendment is frozen.
#
# CLAUDE.md §3 orders this: design the gate (5), dry-run EVERY criterion against the baseline (6),
# prove non-vacuity (7), and only THEN freeze (8). GATE_A3.md is therefore still unfrozen while
# this runs, and nothing in it may be edited afterwards.
#
# FOUR LEGS, in GATE_A3.md §5's order:
#   M1   pre-fix library (locator fix reverse-applied), deadBand 3
#        REQUIRED: C5c FAILS (front 51.267 cells out vs a 0.08 mm bound), C5a/C5b/C5d PASS
#   REF  post-fix library, deadBand 3 -- the gate's own configuration
#   M2   post-fix library, deadBand 1 (the minimum :147 permits), NO rebuild needed
#        REQUIRED: C5b FAILS. This also TESTS verification_A3.md §4's unestablished claim that
#        min Y_i tracks the fill count. If C5b passes here, the mechanism is wrong, C5b has no
#        control, and the amendment STOPS at C5b rather than being frozen.
#   M3   post-fix + the reactor's heat release scaled by 0.9999 at PremixedFlame.cpp:313
#        REQUIRED: C5a FAILS. :311's comment states that line shares cp and the formation
#        enthalpies with the energy equation "so the two agree on T_ad by construction"; scaling
#        it breaks exactly that agreement, landing the plateau at 1799.85 K -- 0.15 K low, which
#        misses the 1.8e-03 K bound by ~83x. The probe's T_ad is computed independently and is
#        NOT affected.
#
# WHY NOT THE OBVIOUS M3. The first candidate was the heatRelease at PremixedFlame.cpp:1086. It is
# DEAD CODE -- computed and never used (no -Wunused warning because ScalarField has a non-trivial
# constructor). Patching it would have produced a control that silently could not fail. Checked
# before use rather than after.
#
# src/species/PremixedFlame.cpp is UNTRACKED, so `git checkout` cannot restore it. A byte copy is
# taken first and the EXIT trap restores and hash-verifies.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
SRC=$REPO/src/species/PremixedFlame.cpp
BACKUP=$ITEM/tools/.PremixedFlame.cpp.a3backup
OUT=$ITEM/logs/A3_01_dryrun.log
PROBE=$ITEM/tools/c5_amended_probe.cpp
BIN=$ITEM/tools/td004_c5a3
POST_SRC=fa815c9721658448

if [ -e "$OUT" ]; then
  echo "*** ABORT: $OUT exists. Refusing to overwrite a recorded result."
  exit 1
fi
LOAD=$(awk '{print int($1)}' /proc/loadavg)
if [ "$LOAD" -gt 6 ]; then
  echo "*** ABORT: load average $LOAD -- another authoritative run may hold the machine."
  exit 1
fi
CUR=$(sha256sum "$SRC" | cut -c1-16)
if [ "$CUR" != "$POST_SRC" ]; then
  echo "*** ABORT: tree is not the post-fix source. want $POST_SRC, got $CUR"
  exit 1
fi
cp -p "$SRC" "$BACKUP" || { echo "*** ABORT: backup failed"; exit 1; }

restore() {
  echo
  echo "=== EXIT TRAP: restoring the post-fix source"
  if [ -e "$BACKUP" ]; then
    cp -p "$BACKUP" "$SRC"
    R=$(sha256sum "$SRC" | cut -c1-16)
    if [ "$R" = "$POST_SRC" ]; then
      echo "restored and hash-verified: $R"
      rm -f "$BACKUP"
      cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 \
        && echo "library rebuilt post-fix" || echo "*** rebuild FAILED -- rebuild cfdcore manually"
    else
      echo "*** RESTORE MISMATCH: got $R want $POST_SRC. BACKUP KEPT at $BACKUP"
    fi
  else
    echo "*** NO BACKUP -- inspect $SRC manually"
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
echo "  GATE_A3.md is NOT yet frozen -- this dry-run is what qualifies it for freezing."
echo

build_and_run() {
  local label="$1" band="$2"
  cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build ($label)"; exit 1; }
  echo "library  $(sha256sum "$BUILD/src/libcfdcore.a" | cut -c1-32)"
  /usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
    -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
    -Wall -Wextra -Wpedantic "$PROBE" -o "$BIN" "$BUILD/src/libcfdcore.a" \
    || { echo "*** ABORT: compile ($label)"; exit 1; }
  "$BIN" "$band" "$label"
}

echo "############################################################ M1  pre-fix, deadBand 3"
python3 - "$SRC" <<'PYEOF'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8", newline="").read()
n1 = "    previousLocated -= static_cast<Real>(shift) * cellSize;\n"
if n1 not in s:
    n1 = n1.replace("\n", "\r\n")
if s.count(n1) != 1:
    sys.stderr.write("*** ABORT: locator-fix line appears %d times, expected 1\n" % s.count(n1))
    sys.exit(1)
io.open(p, "w", encoding="utf-8", newline="").write(s.replace(n1, ""))
print("patched: removed exactly 1 line (the locator fix)")
PYEOF
[ $? -eq 0 ] || { echo "*** ABORT: M1 patch"; exit 1; }
build_and_run M1_prefix 3.0
echo

echo "############################################################ restore post-fix"
cp -p "$BACKUP" "$SRC"
[ "$(sha256sum "$SRC" | cut -c1-16)" = "$POST_SRC" ] || { echo "*** ABORT: restore mismatch"; exit 1; }
echo "post-fix source restored"
echo
echo "############################################################ REF  post-fix, deadBand 3"
build_and_run REF_postfix 3.0
echo

echo "############################################################ M2  post-fix, deadBand 1"
echo "  (same library -- only the setting differs, so no rebuild)"
"$BIN" 1.0 M2_deadband1
echo

echo "############################################################ M3  heat release x 0.9999"
python3 - "$SRC" <<'PYEOF'
import io, sys
p = sys.argv[1]
s = io.open(p, encoding="utf-8", newline="").read()
old = "cellTemperature[0] += step * heat[0] / (cellDensity[0] * specificHeat);"
new = "cellTemperature[0] += 0.9999 * step * heat[0] / (cellDensity[0] * specificHeat);"
if s.count(old) != 1:
    sys.stderr.write("*** ABORT: the reactor T update appears %d times, expected 1\n" % s.count(old))
    sys.exit(1)
io.open(p, "w", encoding="utf-8", newline="").write(s.replace(old, new))
print("patched: reactor heat release scaled by 0.9999 (exactly 1 site)")
PYEOF
[ $? -eq 0 ] || { echo "*** ABORT: M3 patch"; exit 1; }
build_and_run M3_heat9999 3.0
echo

echo "############################################################ NON-VACUITY SUMMARY"
echo
grep "MACHINE-READABLE" "$OUT" | sed 's/^ *//'
echo
echo "REQUIRED, from GATE_A3.md §3.1:"
echo "  M1  C5c=FAIL  and C5a=PASS C5b=PASS C5d=PASS"
echo "  M2  C5b=FAIL"
echo "  M3  C5a=FAIL"
echo
echo "Each of C5a, C5b, C5c must have FAILED on at least one leg. A clause that never failed is"
echo "VACUOUS and GATE_A3.md may NOT be frozen -- CLAUDE.md §12 requires stopping and reporting."
echo "The verdict is written in dry_run_A3.md, not asserted here."
echo
echo "DONE $(date -Is)"

#!/usr/bin/env bash
# TD-005 -- FREEZE THE GATE. Writes logs/00_freeze.log, which is what apply_c4d_fix.py requires
# to exist before it will touch production.
#
# CLAUDE.md section 3.8: "Freeze the gate: record its sha256 plus production, test and library
# hashes in logs/00_freeze.log." This runs BEFORE any production change, and the fix script is
# guarded on this file's existence so the order cannot be got wrong by accident.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EV="$REPO/results/technical_debt/TD005_flame_speed_extraction"
BUILD="$REPO/build/release"

cd "$REPO" || exit 1

echo "TD-005 -- GATE FREEZE"
echo "frozen $(date -Is)"
echo
echo "=== THE GATE"
sha256sum "$EV/GATE.md"
echo
echo "=== THE EVIDENCE IT WAS FROZEN AGAINST (dry-run complete for EVERY criterion)"
sha256sum "$EV/audit.md" "$EV/dry_run.md"
for f in 01_reproducibility_baseline 02_field_dump 03_tests_at_repo_cap \
         04_controls_dryrun_INVALID_construction 05_controls_dryrun; do
  sha256sum "$EV/logs/$f.log"
done
echo
echo "=== PRODUCTION, UNCHANGED AT THE MOMENT OF FREEZING"
sha256sum "$REPO/src/species/PremixedFlame.cpp" "$REPO/include/cfd/species/PremixedFlame.hpp"
echo
echo "=== TESTS, UNCHANGED"
sha256sum "$REPO/tests/unit/species/test_premixed_flame.cpp"
echo
echo "=== LIBRARY AND TEST BINARY THE BASELINE WAS MEASURED WITH"
sha256sum "$BUILD/src/libcfdcore.a" "$BUILD/tests/unit/species/CFDSpeciesTests"
echo
echo "=== PROBES"
for p in reproducibility_probe field_dump_probe controls_probe; do
  sha256sum "$EV/tools/$p.cpp"
done
echo
echo "=== ENVIRONMENT"
echo "compiler $(/usr/bin/c++ --version | head -1)"
grep -E '^(CMAKE_BUILD_TYPE|CFDAPP_ENABLE_OPENMP|CFDAPP_ENABLE_CUDA)' "$BUILD/CMakeCache.txt"
echo "nproc $(nproc)"
echo
cat <<'TXT'
=== DRY-RUN SUMMARY, as frozen

  C1   PASS   bitwise flameSpeed == massFlux/unburntDensity, both modes      logs/02
  C2   PASS   brackets honest; ladder converges inside the derived bounds    logs/01 logs/05
  C3   FAIL   5 of 12 configurations, 0.2380..0.3721 against < 0.05          logs/01
  C4a  PASS   plateau test 997 s against the 7200 s cap; no change needed    logs/03
  C4b  PASS   0.001355 <= 0.02                                               logs/01 logs/03
  C4c  PASS   exactly 0 against 6.77e-04                                     logs/01
  C4d  FAIL   48.2 cells at the bad mode against <= 1; 0.3 at the good       logs/02
  C5   1 of 2 controls demonstrated; the no-flame control is UNMEASURED      logs/05
  C6   FAIL   by construction -- the TD-003 bound is bundled into the test   audit 6
  C7   PASS   343, matching TD-004's baseline for the same exclusion         logs/03

C3, C4d and C6 fail on the unchanged tree BECAUSE THEY ARE THE DEFECTS. Every other criterion
passes, so the gate is neither vacuous nor trivially satisfiable, and C3/C4d are each proved
non-vacuous in BOTH directions by the baseline alone, with nothing constructed.

=== AUTHORIZED CHANGE IN HAND

"fix the report" -- C4d ONLY. C3 and C6 remain open, and closing C4d does not close TD-005.
TXT
echo
echo "FROZEN. No threshold, criterion or control in GATE.md may now change without a separately"
echo "authorized amendment carrying its own dry-run (CLAUDE.md 5.3)."

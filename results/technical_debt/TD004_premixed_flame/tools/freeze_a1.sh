#!/usr/bin/env bash
# TD-004 AMENDMENT A1 -- freeze. CLAUDE.md 3.8 and 5.3: an amendment is frozen as its own file,
# with its own dry-run, and its hashes recorded before any verdict is taken.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EV="$REPO/results/technical_debt/TD004_premixed_flame"
TD5="$REPO/results/technical_debt/TD005_flame_speed_extraction"
BUILD="$REPO/build/release"
cd "$REPO" || exit 1

echo "TD-004 AMENDMENT A1 -- FREEZE"
echo "frozen $(date -Is)"
echo
echo "=== THE AMENDMENT"
sha256sum "$EV/GATE_A1.md"
echo
echo "=== THE ORIGINAL GATE, UNCHANGED (A1 supersedes its C2 and nothing else)"
sha256sum "$EV/GATE.md"
echo
echo "=== THE EVIDENCE A1 IS DERIVED FROM"
sha256sum "$EV/verification.md" "$EV/summary.md" "$TD5/dry_run.md"
for f in 10_front_trajectory 13_falsification 14_period 15_decay 19_cycle_at_dt4 20_fuel_budget; do
  sha256sum "$TD5/logs/$f.log"
done
echo
echo "=== PRODUCTION AND TESTS AT THE MOMENT OF FREEZING"
sha256sum "$REPO/src/species/PremixedFlame.cpp" \
          "$REPO/include/cfd/species/PremixedFlame.hpp" \
          "$REPO/tests/unit/species/test_premixed_flame.cpp"
echo
echo "=== LIBRARY"
sha256sum "$BUILD/src/libcfdcore.a"
echo
echo "=== THE AMENDMENT'S OWN PROBE"
sha256sum "$EV/tools/c2a1_probe.cpp"
echo
cat <<'TXT'
=== WHAT A1 CHANGES, as frozen

  REPLACED   C2  "the balance no longer degrades with march length; ratio <= 2.0"
                 Premise FALSE: the signal is a LIMIT CYCLE of constant amplitude, so there is
                 no ratio to bound and no fix to the recentring operator could have satisfied it.
                 The measured 46.9362 stands as a real measurement; the criterion was wrong.

  WITH       C2-A1 over relaxT {10, 14, 18, 22, 26, 30} at GATE.md's fixture:
                 (a) REACHES  some level has fuelBalanceResidual < 0.05
                 (b) STAYS    every level at or beyond it is also admissible
                 (c) SETTLES  over those levels, (max - min) <= mean      [no constant]

  THRESHOLD PROVENANCE  0.05 is the tree's OWN bound (test_premixed_flame.cpp:447), neither
                        weakened nor tightened, and the same bound TD-005 C3 uses. Clause (c)
                        carries NO constant: a quantity varying by more than its own mean has
                        not settled, which is scale-free by construction.
  FAMILY PROVENANCE     sized to RESOLVE the measured 14-relaxation-time period -- 1.43 periods
                        at 3.5 points per period. The family is sized from a measured property
                        of the SYSTEM, as a mesh is sized from a measured flame thickness. No
                        THRESHOLD is taken from observed output (CLAUDE.md 5.3).

  UNCHANGED  C1, C3, C4, C5, C6, C6a, C6b and C7 stand exactly as frozen in GATE.md.

=== DRY-RUN, as frozen (GATE_A1.md section 5)

  every clause has a real PASS case AND a real FAIL case, from measured data, nothing constructed
  on the unchanged baseline C2-A1 FAILS at (b); (c) also fails
  C2-A1 is SATISFIABLE: logs/19's sub-family satisfies (b) and (c) by 47x, the baseline satisfies
  (a); it requires TD-028 and TD-030's candidate to be resolved

  PREDICTED for the fresh rerun: FAIL at (b)
TXT
echo
echo "FROZEN. Rerun fresh per CLAUDE.md 5.3 -- no earlier result may stand in for the verdict."

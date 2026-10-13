#!/usr/bin/env bash
# TD-027 AMENDMENT A1 -- freeze. Adds criterion C7 (the verdict-movement accounting the original
# gate lacked). No threshold anywhere is changed.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EV="$REPO/results/technical_debt/TD027_cell_diffusion_cap"
BUILD="$REPO/build/release"
cd "$REPO" || exit 1

echo "TD-027 AMENDMENT A1 -- FREEZE"
echo "frozen $(date -Is)"
echo
echo "=== THE AMENDMENT"
sha256sum "$EV/GATE_A1.md"
echo
echo "=== THE ORIGINAL GATE AND AUDIT, WHICH MUST BE BIT-IDENTICAL TO logs/00_freeze.log"
sha256sum "$EV/GATE.md" "$EV/audit.md"
echo "    expected  56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75  GATE.md"
echo "    expected  abb4a6202c7dafe011ded5ec901cd7fc025267ebb1df12374a7300b0c5d52df9  audit.md"
echo
echo "=== THE EVIDENCE C7 IS EVALUATED AGAINST (measured BEFORE the amendment -- see GATE_A1 5)"
for f in 01_verify_c1_c4 02_c1_rerun 03_regression_A 04_regression_B 05_regression_C \
         06_regression_D 08_stage_d_control; do
  sha256sum "$EV/logs/$f.log"
done
echo
echo "=== THE FORWARD CORRECTIONS THAT ACCOMPANY THIS AMENDMENT"
sha256sum "$EV/attribution_correction.md" "$EV/regression.md"
echo
echo "=== PRODUCTION AS SHIPPED BY TD-027"
sha256sum "$REPO/include/cfd/species/PremixedFlame.hpp"
grep -n 'maximumCellDiffusionNumber{' "$REPO/include/cfd/species/PremixedFlame.hpp"
echo
echo "=== LIBRARY AND TEST BINARY"
sha256sum "$BUILD/src/libcfdcore.a" "$BUILD/tests/unit/species/CFDSpeciesTests"
echo
cat <<'TXT'
=== WHAT A1 ADDS

  C7  every flame-suite verdict movement is NAMED, with its cause and the item that owns it.
      Threshold: the set of observed movements must EQUAL the named set. Any movement the
      amendment does not name FAILS C7. No numerical threshold -- nothing in it to weaken.

=== WHAT A1 DOES NOT DO

  * it changes NO threshold, in this gate or in any test
  * it does NOT make the two PASS -> FAIL failures acceptable. They are real, they are owned by
    TD-029 and TD-030, and both items stay OPEN
  * it does NOT amend GATE.md or audit.md, whose hashes are the freeze record. The refuted
    cap-governs-the-cycle attribution they contain is corrected FORWARD in
    attribution_correction.md, for the same reason

=== THE TWELVE NAMED MOVEMENTS

  PASS -> FAIL   ItConverges...                 TD-030   converged balance 0.0847 vs < 0.02
  PASS -> FAIL   TheFlameSpeedConverges...      TD-029   refinement differences GROW
  FAIL -> FAIL   ThePlateau...                  TD-031   now 2 anchors; front 32-36 cells off
  FAIL -> FAIL   TheConsumptionIntegral...      TD-030   failing CLAUSE changed
  FAIL -> FAIL   AVariableConductivity...       pre-existing fixture arithmetic
  UNMEAS -> PASS WithReactionsDisabled...       TD-004's locator fix, NOT TD-027 (logs/08)
  PASS -> PASS   TheAnswerIsInsensitive...      TD-005 F1 RESOLVED: spread exactly 0
  PASS -> PASS   TheEigenvalueIsIndependent...  TD-031   S_c 20 % / delta 30 % state spread
  PASS -> PASS   TenTimesThePreExponential...   S_L down ~4 % as C2 requires
  PASS -> PASS   three instant cases            unchanged

=== OUTSTANDING CORROBORATION, recorded rather than waited for

  TD-031's stage 2 re-measures 500 cells / cap 5 -- the configuration C4 was verified at -- on a
  different relaxT ladder. It is NOT a control on C4, whose verdict comes from this item's own
  logs/01, but a disagreement would be a reproducibility problem worth knowing about. In flight
  at freeze time.
TXT
echo
echo "FROZEN. C1-C7 all PASS; TD-027 is closeable with its named failures owned elsewhere."

#!/usr/bin/env bash
# TD-027 -- FREEZE THE GATE. CLAUDE.md 3.8: record the gate's sha256 plus the production, test and
# library hashes BEFORE any production change. apply_fix.py refuses to run until this file exists.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EV="$REPO/results/technical_debt/TD027_cell_diffusion_cap"
TD4="$REPO/results/technical_debt/TD004_premixed_flame"
TD5="$REPO/results/technical_debt/TD005_flame_speed_extraction"
BUILD="$REPO/build/release"
cd "$REPO" || exit 1

echo "TD-027 -- GATE FREEZE"
echo "frozen $(date -Is)"
echo
echo "=== THE GATE"
sha256sum "$EV/GATE.md" "$EV/audit.md"
echo
echo "=== PRODUCTION, UNCHANGED AT THE MOMENT OF FREEZING"
sha256sum "$REPO/include/cfd/species/PremixedFlame.hpp" "$REPO/src/species/PremixedFlame.cpp"
echo
echo "=== TESTS, UNCHANGED"
sha256sum "$REPO/tests/unit/species/test_premixed_flame.cpp"
echo
echo "=== LIBRARY"
sha256sum "$BUILD/src/libcfdcore.a"
echo
echo "=== THE EVIDENCE THE DRY-RUN CITES (non-vacuity; the verdicts must be fresh)"
sha256sum "$TD5/logs/18_dt_ladder.log" "$TD5/logs/19_cycle_at_dt4.log" \
          "$TD5/logs/03_tests_at_repo_cap.log" "$TD5/logs/08_regression.log" \
          "$TD4/logs/A1_01_c2a1_verification.log"
echo
echo "=== THE THREE FROZEN GATE FILES THIS FIX INVALIDATES VALUES IN (must NOT be edited)"
sha256sum "$TD4/GATE.md" "$TD4/GATE_A1.md" "$TD5/GATE.md"
echo
cat <<'TXT'
=== THE CHANGE UNDER TEST

  include/cfd/species/PremixedFlame.hpp
     Real maximumCellDiffusionNumber{20.0};   ->   {5.0}

  dt = cap * dx^2 / alpha_max, and the diffusion limit binds at the eigenvalue, so D IS the cap
  (audit section 1). dt therefore falls 4x for every premixed-flame solve.

=== CRITERIA, as frozen

  C1  the cap IS the realized D         alpha_max*dt/dx^2 == cap to 1e-12 relative
  C2  S_L within the temporal budget    |S_L - 4.67589|/4.67589 <= 0.28 %   (G8's band / 10)
  C3  reported precision coherent       error / (sqrt(1+tol)-1) <= 2
  C4  the limit cycle is suppressed     (max-min) <= mean over relaxT {18,22,26,30}
  C5  nothing else changes              343 / 331 / 32
  C6  no test breaches 7200 s           every case produces a verdict; longest <= 7200 s

  CONVERGED REFERENCE, fixed before any criterion uses it:  S_L = 4.67589 cm/s
  (Richardson over {dt/2, dt/4, dt/8}, p = 2.321, GCI 0.021 %). This REPLACES 4.86230, which was
  measured at D = 20, outside the asymptotic range, and is 3.99 % high.

=== DRY-RUN, as frozen (GATE.md section 6)

  C2, C3 and C4 all FAIL on the unchanged tree -- they ARE the defect -- and all three are
  demonstrated to PASS at cap = 5 on data measured before this gate was written.
  C1, C5, C6 pass on the unchanged tree. Every criterion has a real pass AND a real fail case.

=== EMPIRICAL CONSTANTS, labelled per CLAUDE.md 5.1

  C2's "one tenth of G8's band"   an engineering margin, so the time discretisation is not a
                                  material contributor to a gated comparison
  C3's "factor of 2"              within 2x, a reported uncertainty and the true error are the
                                  same order and the report is not misleading
  C4 carries NO constant.
TXT
echo
echo "FROZEN. No threshold or criterion may change without a separately authorized amendment."

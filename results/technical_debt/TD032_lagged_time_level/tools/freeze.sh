#!/bin/bash
# TD-032 -- FREEZE THE GATE. Run only after dry_run.md is complete and every criterion has been
# dry-run against the unchanged tree (CLAUDE.md 3, steps 5-8).
#
# This records the sha256 of the gate, of every production and test file in scope, of the library
# the dry-run measured, and of the probe sources and binaries -- so that any later claim about
# "before" and "after" is checkable against bytes rather than against a narrative.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD032_lagged_time_level

echo "=== TD-032 GATE FREEZE"
echo "date   $(date -Is)"
echo
echo "The gate is frozen at the hashes below. Any change to GATE.md after this point is an"
echo "AMENDMENT and needs its own file, its own dry-run and its own authorization (CLAUDE.md 5.3)."
echo

echo "--- THE GATE"
sha256sum "$ITEM/GATE.md"
sha256sum "$ITEM/dry_run.md"
sha256sum "$ITEM/audit.md"
echo

echo "--- PRODUCTION FILES IN SCOPE (the pre-fix state, which the fix will change)"
sha256sum "$REPO/src/species/PremixedFlame.cpp"
sha256sum "$REPO/include/cfd/species/PremixedFlame.hpp"
echo

echo "--- TEST FILES (must be UNCHANGED by this item; recorded so that is checkable)"
sha256sum "$REPO/tests/unit/species/test_premixed_flame.cpp"
sha256sum "$REPO/tests/unit/thermal/test_thermal_transient.cpp"
sha256sum "$REPO/tests/unit/transport/test_scalar_transport.cpp"
echo

echo "--- THE LAGGED LIBRARY AND BINARIES THE DRY RUN MEASURED (C8's negative control)"
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$BUILD/tests/unit/io/CFDIoTests"
sha256sum "$BUILD/tests/unit/app/CFDAppLayerTests"
sha256sum "$BUILD/tests/unit/species/CFDSpeciesTests"
echo

echo "--- PROBES"
sha256sum "$ITEM/tools/alternation_probe.cpp"
sha256sum "$ITEM/tools/residual_vs_length_probe.cpp"
sha256sum "$ITEM/tools/td032_alternation"
sha256sum "$ITEM/tools/td032_residual"
echo

echo "--- THE FROZEN THRESHOLDS, restated so the log stands alone"
cat <<'THRESHOLDS'
C0     instrument self-test: (a) R <= 1 with a meaningful ratio, (b) R > 1, (c) vacuous+FAIL,
       (d) vacuous+PASS
C1     R = across/within <= 1.0   500 cells, cap 5, relaxT 18, budgets 30945..30949
C2     R <= 1.0                   250 cells, cap 5, relaxT 18, budgets 7256..7260
       vacuity branch for both: within <= 1e-9 K -> R not evaluated, require across <= 1e-9 K
C3(a)  temperatureResidual <= 1.0e-2   at the gated fixture
C3(b)  temperatureResidual(relaxT 26) <= 0.5 * temperatureResidual(relaxT 18)
C4     maxCompositionSumDeviation <= 2.0e-06   (EMPIRICAL, from the worst baseline branch)
C5     |S_L - 4.671937| / 4.671937 <= 3.99 %
C6     CFDIoTests 331 / CFDAppLayerTests 32 / CFDSpeciesTests 343 under
       --gtest_filter=-G8PremixedFlameTest.*
C7     median of 4 post-warm-up gated-fixture solves <= 695.1 s; no test breaches 7200 s
C8     the lagged library fails C1 and C2 -- SATISFIED by logs/01, hash recorded above
THRESHOLDS
echo
echo "FROZEN $(date -Is)"

#!/bin/bash
# TD-004 -- THE MEASUREMENTS C6 AND C6a NEED. The gate has never reached either.
#
# WHAT C6 AND C6a REQUIRE, quoted from the frozen GATE.md:
#   C6   "the eigenvalue still has its qualified properties | properties 2, 3, 5 of audit.md §5 --
#        independence of the initial guess, plateau in anchor and domain, convergence under
#        refinement | each still holds; S_L reported with its change from the baseline 4.86230 cm/s"
#   C6a  "the refinement family is RE-DERIVED on CONSERVING states | rerun tools/fixture_probe.cpp
#        after the fix | the {125, 250, 500} triplet still admissible (every level converged, every
#        level > 4 cells of front, differences shrink, no sign change); observed order p in
#        [1.5, 2.5]; and the fuel balance at every level < 0.02"
#
# WHY THE MEASUREMENT ORDER IS NOT THE VERDICT ORDER. C6's property 5 IS refinement convergence, so
# it cannot be judged without C6a's probe output. The frozen order governs which VERDICT is read
# first and where the gate stops -- not which solve runs first. Verdicts are read C6 -> C6a -> C6b
# -> C7 in dry_run/verification, and the gate stops at the first failing one.
#
# C6a's INSTRUMENT IS NOT MODIFIED. GATE.md names tools/fixture_probe.cpp, logs/00_freeze.log
# records it at e9d83b3a0740b2a5..., and this harness ABORTS if it differs. It does not report
# consumptionSpeed, so C6b needs a separate instrument at the SAME fixture -- that is a later run,
# not a patch to this one.
#
# PROPERTIES 2 AND 3 ARE MEASURED BY THE PROJECT'S OWN TESTS, not by a new probe:
#   TheEigenvalueIsIndependentOfTheInitialMassFluxGuess          -> property 2
#   TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength   -> property 3
# Both use cells 500 and maximumStepsPerTrial 20000, the same family fixture_probe uses, so the
# three properties are measured on ONE consistent fixture. Writing fresh probes for properties the
# suite already asserts would risk measuring something subtly different.
#
# TheFlameSpeedConvergesUnderMeshRefinement is DELIBERATELY NOT RUN: it uses {250, 500, 1000} and
# logs/07 records that it does not finish in 1750 s. That infeasibility is the whole reason C6a
# exists to re-derive a cheaper family. Running it would burn the per-test cap to re-learn a
# recorded fact.
set -u
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
ITEM=$REPO/results/technical_debt/TD004_premixed_flame
OUT=$ITEM/logs/A5_01_c6_c6a.log
PROBE=$ITEM/tools/fixture_probe.cpp
FROZEN_PROBE=e9d83b3a0740b2a599fc1225b7e84b56294e1d5a419e243553ab0598ff76b403
POST_SRC=fa815c97216584485e1c94842669fbf71976b9fb06863096868972d88448e3a2
POST_LIB=852555cfda54f8da013bde93f9c205c15daf2e5291d7920a3744b86e9853c7f8

[ -e "$OUT" ] && { echo "*** ABORT: $OUT exists."; exit 1; }
LOAD=$(awk '{print int($1)}' /proc/loadavg)
[ "$LOAD" -gt 6 ] && { echo "*** ABORT: load $LOAD"; exit 1; }

exec > >(tee "$OUT") 2>&1

echo "=== ENVIRONMENT"
echo "date   $(date -Is)"
echo "host   $(uname -sr)"
echo "nproc  $(nproc)   load $(cat /proc/loadavg)"
grep CMAKE_BUILD_TYPE "$BUILD/CMakeCache.txt"
echo

echo "=== GATE: the named instrument must be the frozen one"
P=$(sha256sum "$PROBE" | cut -d' ' -f1)
echo "  fixture_probe.cpp  $P"
if [ "$P" != "$FROZEN_PROBE" ]; then
  echo "*** ABORT: fixture_probe.cpp differs from logs/00_freeze.log."
  echo "    C6a says RERUN this probe. A modified probe is a different instrument."
  exit 1
fi
echo "  OK: matches logs/00_freeze.log."
echo
echo "=== GATE: the tree and library under test"
S=$(sha256sum "$REPO/src/species/PremixedFlame.cpp" | cut -d' ' -f1)
echo "  PremixedFlame.cpp  $S"
[ "$S" = "$POST_SRC" ] || { echo "*** ABORT: not the post-fix source."; exit 1; }
cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1 || { echo "*** ABORT: build"; exit 1; }
L=$(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)
echo "  libcfdcore.a       $L"
[ "$L" = "$POST_LIB" ] || { echo "*** ABORT: wrong library: $L"; exit 1; }
echo "  OK: post-fix source, uncorrupted post-fix library."
echo

echo "############################################################ C6a / C6 property 5"
echo "  rerunning the FROZEN fixture_probe.cpp, unmodified"
echo
/usr/bin/c++ -I"$REPO/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -O3 -DNDEBUG -std=c++20 \
  -Wall -Wextra -Wpedantic "$PROBE" -o "$ITEM/tools/td004_fixture_post" \
  "$BUILD/src/libcfdcore.a" || { echo "*** ABORT: compile fixture_probe"; exit 1; }
"$ITEM/tools/td004_fixture_post"
echo

echo "############################################################ C6 properties 2 and 3"
echo "  the project's own tests, on the fixed tree. Baseline for comparison, from"
echo "  logs/06_flame_baseline_per_test.log (PRE-fix, PRE-TD-032):"
echo "    mdot_0 = 0.1      S_L 4.86245     mdot_0 = 0.9   S_L 4.86017"
echo "    anchor 0.30       S_L 4.85572     anchor 0.40    S_L 4.86230"
echo "    C6's stated baseline S_L: 4.86230 cm/s"
echo
cmake --build "$BUILD" --target CFDSpeciesTests -j 20 >/dev/null 2>&1 \
  || { echo "*** ABORT: test build"; exit 1; }
TB=$(find "$BUILD" -name 'CFDSpeciesTests' -type f -perm -u+x | head -1)
echo "  test binary: $TB"
sha256sum "$TB"
echo "  binary mtime $(stat -c%y "$TB" | cut -c1-19)  vs source $(stat -c%y "$REPO/src/species/PremixedFlame.cpp" | cut -c1-19)"
if [ "$TB" -ot "$REPO/src/species/PremixedFlame.cpp" ]; then
  echo "*** ABORT: the test binary is OLDER than the production source (CLAUDE.md §8)."
  exit 1
fi
echo "  OK: binary is newer than the source it tests."
echo
"$TB" --gtest_filter='G8PremixedFlameTest.TheEigenvalueIsIndependentOfTheInitialMassFluxGuess:G8PremixedFlameTest.TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength' \
  --gtest_brief=0
echo "  gtest exit: $?"
echo
echo "=== WHAT IS DELIBERATELY NOT RUN HERE"
echo "  TheFlameSpeedConvergesUnderMeshRefinement -- {250,500,1000}, does not finish in 1750 s"
echo "  (logs/07). C6a exists to re-derive that family; property 5 is judged from C6a's probe."
echo "  C6b -- needs consumptionSpeed, which fixture_probe does not report. Separate run."
echo "  C7  -- CFDSpeciesTests excluding G8PremixedFlameTest. Separate run, and it comes LAST"
echo "         in the frozen order."
echo
echo "DONE $(date -Is)"

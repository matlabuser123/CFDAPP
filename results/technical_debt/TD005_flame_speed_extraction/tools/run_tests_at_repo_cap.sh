#!/usr/bin/env bash
# TD-005 -- MEASURE THE "TIMEOUT" TESTS AT THE REPOSITORY'S OWN CAP.
#
# WHY THIS EXISTS. TD-004's per-test baseline (logs/06) recorded three G8PremixedFlameTest cases as
# "TIMEOUT after 900s", and TD-005's gate first draft treated that as a repository fact. It is not:
# that log's own header says "per-test timeout 900s", which was TD-004's HARNESS choice. Measured:
#
#   tests/unit/species/CMakeLists.txt:37   gtest_discover_tests(CFDSpeciesTests ...)
#                                          -> every gtest case is its OWN ctest entry
#   .github/workflows/ci.yml:147           ctest ... --timeout 7200
#   grep -rn TIMEOUT over the CMake tree   -> no per-test TIMEOUT property anywhere
#
# So the repository's cap is 7200 s PER CASE. GATE.md section 5.2 therefore may not name these as
# permitted non-runners until they have been measured at 7200 s, and C4(a) is specified as
# "produces a verdict under --timeout 7200".
#
# WHAT IS MEASURED HERE, and what is deliberately left for its own run:
#   plateau            C4(a). ~4 solves at ~360 s = ~1450 s expected.
#   mesh refinement    reported, NOT gated (TD-004/TD-006 territory). ~3 solves, cheaper levels.
#   reactions disabled NOT here -- TD-020's entry describes a cost that may genuinely approach the
#                      full 7200 s, so it gets its own run (logs/03) rather than crowding this one.
#
# FAIL-CLOSED: the test binary is rebuilt and its freshness proved BEFORE anything runs. An
# existence test is not a freshness test, and this campaign has already had a stale binary print
# results that read as current.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD="$REPO/build/release"
BIN="$BUILD/tests/unit/species/CFDSpeciesTests"
CAP=7200

cd "$REPO" || exit 1

echo "TD-005 -- G8PremixedFlameTest at the REPOSITORY cap"
echo "per-test timeout ${CAP}s   (ci.yml: ctest --timeout 7200; gtest_discover_tests => per case)"
echo "started $(date -Is)"
echo "nproc $(nproc)   $(/usr/bin/c++ --version | head -1)"
echo

echo "=== REBUILD the test binary against the current source"
cmake --build "$BUILD" --target CFDSpeciesTests -j 20
rc=$?
echo "build exit: $rc"
if [ "$rc" -ne 0 ]; then
  echo "*** ABORT: the test binary did not build. No stale binary is run."
  exit 1
fi

echo
echo "=== FRESHNESS (CLAUDE.md section 8: the binary must be newer than the newest production source)"
newest=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs ls -t | head -1)
echo "newest source : $newest"
if [ "$BIN" -nt "$newest" ]; then
  echo "binary is NEWER than the newest production source: OK"
else
  echo "*** ABORT: the test binary is not newer than $newest"
  exit 1
fi
sha256sum "$BIN" "$REPO/src/species/PremixedFlame.cpp" "$REPO/tests/unit/species/test_premixed_flame.cpp"
echo

run_one() {
  name="$1"
  note="$2"
  echo "=== $name"
  echo "    ($note)"
  start=$(date +%s)
  timeout "$CAP" "$BIN" --gtest_filter="G8PremixedFlameTest.$name" 2>&1 | sed 's/^/    /'
  rc=${PIPESTATUS[0]}
  end=$(date +%s)
  elapsed=$((end - start))
  if [ "$rc" -eq 124 ]; then
    echo "    -> TIMEOUT after ${elapsed}s AT THE REPOSITORY CAP (${CAP}s). This one is real."
  elif [ "$rc" -eq 0 ]; then
    echo "    -> PASS (${elapsed}s)   rc=$rc"
  else
    echo "    -> FAIL (${elapsed}s)   rc=$rc   -- a VERDICT, which is what C4(a) asks for"
  fi
  echo
}

run_one TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength \
        "C4(a): the plateau SELECTS the eigenvalue; recorded TIMEOUT at TD-004's 900 s harness cap"

run_one TheFlameSpeedConvergesUnderMeshRefinement \
        "reported, NOT gated by TD-005 -- TD-004/TD-006 territory"

echo "DONE $(date -Is)"
echo
echo "HOW TO READ THIS"
echo "  a VERDICT (PASS or FAIL) means the 900 s timeout was a harness artefact and the"
echo "    criterion is measurable today, with no code change."
echo "  a TIMEOUT at ${CAP}s means the cost is real, and C4(a) needs the split described in"
echo "    GATE.md 4.4 -- never a raised cap, fewer anchors, or a coarser mesh."

# ---------------------------------------------------------------------------------------------
# C7 -- the suite TD-005 must not disturb. TD-004 measured 343 for this exact exclusion; this
# re-measures it on the CURRENT tree so the gate's expectation is a measurement, not a citation.
echo "=== C7  CFDSpeciesTests excluding G8PremixedFlameTest"
start=$(date +%s)
"$BIN" --gtest_filter=-G8PremixedFlameTest.* 2>&1 | tail -5 | sed 's/^/    /'
echo "    (elapsed $(( $(date +%s) - start ))s)"
echo
echo "C7 EXPECTATION: 343 passing, unchanged from TD-004's baseline for the same exclusion."

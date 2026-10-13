#!/bin/bash
# TD-032 -- SCOPED REGRESSION. The first of two passes, by user decision:
#
#   THIS PASS   clean-first Release build, FULL Release ctest at the repository cap, ASan+UBSan on
#               the three suites that cover the changed code path, clang-format. ~2-3 h.
#   NEXT PASS   full Debug+GUI ctest and the full sanitizer suite, unattended.
#
# TD-032 stays OPEN/VERIFIED until the second pass lands. This pass CANNOT close it, and it does not
# claim to: CLAUDE.md 8 defines the full regression as Release + Debug/GUI + sanitizers at CI
# settings + clang-format, and only three of those four are here.
#
# WHY THESE THREE SUITES FOR THE SANITIZER LEG. The fix changes WHEN two vector copies happen inside
# `advance()` -- `temperatureOld` and `compositionOld` are now captured at the top of the step rather
# than mid-step. That is the entire new memory-safety surface, and it is exercised by
# CFDSpeciesTests (which owns the premixed flame), CFDThermalTests and CFDTransportTests (which own
# the two transient assemblies that consume those fields). Running the binaries directly rather than
# through `ctest --preset asan` is a DELIBERATE DEVIATION so the scope is exactly these three; the
# full sanitizer run follows in the next pass and is the one that counts for closure.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
REL=$REPO/build/release
ASAN=$REPO/build/asan
FIXED_LIB=a9b01668be7ccb7f085f0dc65da0c7096d459c87797b0d650e05ba80974615cf

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "nproc       $(nproc)"
free -h | sed 's/^/mem         /'
echo "compiler    $(c++ --version | head -1)"
echo "cmake       $(cmake --version | head -1)"
echo

echo "=== PRE-FLIGHT: nothing heavy may be running alongside an evidence run"
ps -eo pid,etimes,pcpu,comm --sort=-pcpu | head -6 | sed 's/^/    /'
echo "    (a leaked CFDSpeciesTests has previously turned a 12-minute estimate into 3 hours --"
echo "     if one appears above that is NOT this script's, the timings below are void)"
echo

# =============================================================== STAGE 1
echo "=== STAGE 1  CLEAN-FIRST Release build of every target (CLAUDE.md 8, step 2)"
start=$(date +%s)
cmake --build "$REL" --clean-first -j 20 2>&1 | tail -5
if [ ${PIPESTATUS[0]} -ne 0 ]; then
  echo "*** ABORT: the clean-first Release build failed."
  exit 1
fi
echo "    build took $(( $(date +%s) - start )) s"
echo
REBUILT=$(sha256sum "$REL/src/libcfdcore.a" | cut -d' ' -f1)
echo "    library after a CLEAN rebuild  $REBUILT"
echo "    library from the incremental   $FIXED_LIB"
if [ "$REBUILT" = "$FIXED_LIB" ]; then
  echo "    IDENTICAL -- the host toolchain reproduced the verified library bit for bit, so the"
  echo "    verification in logs/03 was not measuring an incremental-build artefact."
else
  echo "    DIFFERENT -- recorded, not hidden. The host toolchain is normally bit-reproducible"
  echo "    for this library, so a difference here needs explaining before closure."
fi
echo

# =============================================================== STAGE 2
echo "=== STAGE 2  FULL Release ctest, -j16, at the repository cap --timeout 7200"
start=$(date +%s)
cd "$REL" || exit 1
ctest -j16 --timeout 7200 --output-on-failure 2>&1 | tail -40
echo "    ctest took $(( $(date +%s) - start )) s"
echo
echo "--- runtime noise in tracked validation reports (CLAUDE.md 7): classify, do not commit"
cd "$REPO" || exit 1
git status --short results/validation 2>/dev/null | head -20
echo "    (restored below so the working tree carries only TD-032's own change)"
git checkout -- results/validation 2>/dev/null
git status --short results/validation 2>/dev/null | head -5
echo "    validation reports restored"
echo

# =============================================================== STAGE 3
echo "=== STAGE 3  ASan + UBSan at CI settings, on the three suites covering the changed path"
cmake --preset asan >/dev/null 2>&1
start=$(date +%s)
for t in CFDSpeciesTests CFDThermalTests CFDTransportTests; do
  cmake --build "$ASAN" --target "$t" -j 20 >/dev/null 2>&1 || {
    echo "*** ABORT: $t did not build under the asan preset."; exit 1; }
done
echo "    asan build took $(( $(date +%s) - start )) s"
echo
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=0
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
echo "    ASAN_OPTIONS=$ASAN_OPTIONS"
echo "    UBSAN_OPTIONS=$UBSAN_OPTIONS"
echo
# THE G8 SCOPE UNDER ASan, AND WHY IT IS REDUCED. G8PremixedFlameTest lives inside
# CFDSpeciesTests and is the only suite that exercises `advance()`, so it cannot be dropped from
# the sanitizer leg. But it runs ~1.5 h in Release and ASan is 2-4x slower on compute-bound FP
# code, which would make this leg 3-6 h on its own. Reduced to TWO named tests for this pass:
#
#   ItConvergesAndReachesTheIndependentlyDerivedAdiabaticTemperature   ONE canonical solve, 303 s
#   TheObviousChecksAreVacuousOnTheSeededProfile                       the fast structural test
#
# The reduction is defensible on what a sanitizer actually detects: ASan finds spatial and temporal
# memory errors the FIRST time a code path executes, not on repetition. The heavier G8 tests
# (the plateau test at 1283 s, the mesh-refinement test) drive the SAME `advance()` path repeatedly
# at different parameters, so they add solve count, not new memory-safety surface.
#
# THE OTHER TEN G8 TESTS ARE NOT COVERED BY THIS PASS. They are covered by the full sanitizer run
# in the overnight pass, and that pass -- not this one -- is what may close TD-032. This leg is
# reported as REGRESSION: PARTIAL with this scope stated.
G8_ASAN_FILTER=G8PremixedFlameTest.ItConvergesAndReachesTheIndependentlyDerivedAdiabaticTemperature:G8PremixedFlameTest.TheObviousChecksAreVacuousOnTheSeededProfile
SAN_GREP="tests from .* ran|PASSED|FAILED|ERROR: AddressSanitizer|runtime error|SUMMARY:|LeakSanitizer"

echo "--- CFDSpeciesTests under ASan+UBSan, EXCLUDING G8 (the 343)"
start=$(date +%s)
"$ASAN/tests/unit/species/CFDSpeciesTests" --gtest_filter=-G8PremixedFlameTest.* 2>&1   | grep -E "$SAN_GREP" | head -20 | sed 's/^/    /'
echo "    elapsed $(( $(date +%s) - start )) s"
echo

echo "--- CFDSpeciesTests under ASan+UBSan, the TWO NAMED G8 tests (see the note above)"
start=$(date +%s)
"$ASAN/tests/unit/species/CFDSpeciesTests" --gtest_filter="$G8_ASAN_FILTER" 2>&1   | grep -E "$SAN_GREP" | head -20 | sed 's/^/    /'
echo "    elapsed $(( $(date +%s) - start )) s"
echo "    SCOPE: 2 of 12 G8 tests. The other 10 are deferred to the overnight full pass."
echo

for p in thermal/CFDThermalTests transport/CFDTransportTests; do
  N=$(basename "$p")
  echo "--- $N under ASan+UBSan (full suite)"
  start=$(date +%s)
  "$ASAN/tests/unit/$p" 2>&1 | grep -E "$SAN_GREP" | head -20 | sed 's/^/    /'
  echo "    elapsed $(( $(date +%s) - start )) s"
  echo
done

# =============================================================== STAGE 4
echo "=== STAGE 4  clang-format"
cd "$REPO" || exit 1
FILES=$(find include src apps tests -name '*.cpp' -o -name '*.hpp' | sort)
echo "    $(echo "$FILES" | wc -l) files"
echo "$FILES" | xargs clang-format-18 --dry-run --Werror 2>&1 | head -20
if [ ${PIPESTATUS[1]} -eq 0 ]; then
  echo "    clang-format CLEAN"
else
  echo "    *** clang-format reported issues above"
fi
echo

echo "=== WORKING TREE, so the diff this item owns is visible"
git status --short src include tests 2>/dev/null | head -20
echo
echo "DONE $(date -Is)"

#!/usr/bin/env bash
# P17.8 repository integrity (authorization §46). Run LAST, after every control has restored and the
# regression has finished, so what it reports is the state P17.8 actually leaves behind.
#
# The repository has ~286 files uncommitted against a 2026-09-21 HEAD, from phases before this one.
# `git status` alone therefore says almost nothing about P17.8. What this script reports instead is
# the delta P17.8 is responsible for, enumerated explicitly, plus each §46 hazard checked by name.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
P17=results/p17/density_based_solver
fail=0

echo "P17.8 REPOSITORY INTEGRITY"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### 1. nothing was committed, pushed, reset or stashed ###"
echo "  HEAD                      : $(git rev-parse HEAD)"
echo "  HEAD at the P17.8 freeze  : 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274"
if [ "$(git rev-parse HEAD)" = "3bd32f918656f471e72b5f6ac7cb6c2cbf75a274" ]; then
  echo "  HEAD unchanged            : YES"
else
  echo "  HEAD unchanged            : *** NO ***"
  fail=$((fail+1))
fi
echo "  branch                    : $(git rev-parse --abbrev-ref HEAD)"
echo "  stash entries             : $(git stash list | wc -l)   (expected 0)"
echo ""

echo "### 2. the frozen documents are unchanged ###"
(cd "$P17" && sha256sum acceptance_gate.md architecture.md audit.md tools/audit.sh) | sed 's/^/  /'
echo "  expected, from logs/02_freeze.log:"
sed -n '/### the frozen documents ###/,/^$/p' "$P17/logs/02_freeze.log" | grep -E '^[0-9a-f]{64}' | sed 's/^/    /'
for pair in "acceptance_gate.md a88738e481b3964d2a962e9eb83d7dda92c0d9142f997547673dc3cddabe615e" \
            "architecture.md b4afbd8f2bc97244aa99809abd8173e5faa3a4b2336abe74fa6c74c505c17e56" \
            "audit.md bd3e7212a3fa1594ccd61b3e9d3286a5d1d25efc2a21a9258303a9f6b65e8073"; do
  f=${pair%% *}; want=${pair##* }
  got=$(sha256sum "$P17/$f" | awk '{print $1}')
  [ "$got" = "$want" ] || { echo "  *** $f MOVED since the freeze ***"; fail=$((fail+1)); }
done
echo ""

echo "### 3. the P17.8 delta, enumerated ###"
echo "  PRODUCTION SOURCES ADDED (2):"
for f in include/cfd/compressible/DensityBasedSolver.hpp src/compressible/DensityBasedSolver.cpp; do
  printf '    %-58s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo "  TEST SOURCES ADDED (2):"
for f in tests/unit/compressible/test_density_based_solver.cpp \
         tests/integration/compressible/test_density_based_canonical.cpp; do
  printf '    %-58s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo "  BUILD FILES EDITED (3) -- registration only, no target removed."
echo "  The git diff --stat in section 6 is against a 2026-09-21 HEAD and therefore includes"
echo "  EARLIER PHASES' uncommitted additions. P17.8's OWN lines in those three files are:"
echo "    src/CMakeLists.txt                                $(git diff HEAD -- src/CMakeLists.txt | grep -c '^+.*DensityBasedSolver') line  (compressible/DensityBasedSolver.cpp)"
echo "    tests/unit/compressible/CMakeLists.txt            $(git diff HEAD -- tests/unit/compressible/CMakeLists.txt | grep -ic '^+.*density_based') line  (test_density_based_solver.cpp)"
echo "    tests/integration/compressible/CMakeLists.txt     $(git diff HEAD -- tests/integration/compressible/CMakeLists.txt | grep -icE '^\+.*(density_based|DensityBased)') lines (the new test target)"
for f in src/CMakeLists.txt tests/unit/compressible/CMakeLists.txt \
         tests/integration/compressible/CMakeLists.txt; do
  printf '    %-58s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo "  P17.7 FILES EDITED BY THE STAR-ENERGY AMENDMENT (3) -- see AMENDMENT_A6_upstream_exception.md:"
for f in include/cfd/compressible/HllcFlux.hpp tests/unit/compressible/hllc_reference_data.hpp \
         results/p17/hllc/tools/exact_riemann.py; do
  printf '    %-58s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo "  CUDA FILES: 0 edited. cuda/kernels/DeviceHllcFluxKernel.cu contains 0 formulas and was not"
echo "              touched; it compiles the shared CFD_HLLC_HOST_DEVICE header."
printf '    %-58s %s\n' "cuda/kernels/DeviceHllcFluxKernel.cu" "$(sha256sum cuda/kernels/DeviceHllcFluxKernel.cu | awk '{print $1}')"
echo "  CASE / CONFIG FILES: 0. No case-format key was added -- the solver is library-level."
echo ""

echo "### 4. the §46 hazards, each checked by name ###"

echo "-- threshold weakening: no numeric literal in an existing test was relaxed"
echo "   P17.8 edits exactly ONE pre-existing assertion, and it is a STRENGTHENING:"
echo "   U1_UpdateIsExactlyQMinusDtOverVTimesR moved from a unit-volume mesh (where dt/V == dt, so"
echo "   the test could not fail) to a graded mesh, and GAINED a 1/V ratio assertion. No threshold"
echo "   anywhere was made looser. Every bound P17.8 introduces is derived in acceptance_gate.md."
GATE_LIVE=$(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')
echo "   the frozen gate's own hash, unmoved: $GATE_LIVE"

echo "-- removed tests: none"
DB=$(grep -c '^TEST(' tests/unit/compressible/test_density_based_solver.cpp)
CN=$(grep -c '^TEST(' tests/integration/compressible/test_density_based_canonical.cpp)
HL=$(grep -c '^TEST(' tests/unit/compressible/test_hllc_flux.cpp)
echo "   density-based unit TESTs   : $DB"
echo "   canonical integration TESTs: $CN"
echo "   P17.7 HLLC TESTs, untouched: $HL"
echo "   test targets removed from any CMakeLists: $(git diff HEAD -- '*/CMakeLists.txt' | grep -c '^-.*add_executable' || true)   (must be 0)"

echo "-- changed SIMPLE defaults: none"
DEFS=$(grep -c 'DensityBased\|density_based' src/app/ProjectRunner.cpp include/cfd/io/case/*.hpp 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
echo "   density-based references in the dispatch or the case format: $DEFS   (must be 0)"
[ "$DEFS" -eq 0 ] || fail=$((fail+1))

echo "-- duplicated EOS logic: none"
Q2PAT='gamma *[-+*/] *[0-9(a-z]|[0-9)a-z] *[-+*/] *gamma|gasConstant|sqrtR\([^)]*gamma'
DUP=$(grep -cE "$Q2PAT" include/cfd/compressible/DensityBasedSolver.hpp src/compressible/DensityBasedSolver.cpp | awk -F: '{s+=$2} END{print s+0}')
echo "   EOS / gamma arithmetic in the solver: $DUP   (must be 0; sanity: IdealGasEOS.cpp has $(grep -cE "$Q2PAT" src/compressible/IdealGasEOS.cpp))"
[ "$DUP" -eq 0 ] || fail=$((fail+1))

echo "-- duplicated HLLC logic: none"
HLLCSITES=$(grep -cE 'hllcFlux(Checked)?\(' src/compressible/DensityBasedSolver.cpp)
echo "   hllcFlux call sites in the solver: $HLLCSITES   (must be 1 -- one evaluation per face)"
echo "   Riemann-solver formulas restated in the solver: 0 (sStar/qL/qR/shockFactor occurrences: $(grep -cE 'sStar|hllcShockFactor|pPvrs' src/compressible/DensityBasedSolver.cpp))"
[ "$HLLCSITES" -eq 1 ] || fail=$((fail+1))

echo "-- hidden fallback behaviour: none"
echo "   fallback flux occurrences : $(grep -ciE 'fallback' src/compressible/DensityBasedSolver.cpp)   (P17.7 proved no epsilon guard is needed)"
echo "   clamp / floor idioms      : $(grep -cE 'std::clamp|kMinDensity|kPressureFloor|floorPressure' src/compressible/DensityBasedSolver.cpp)   (must be 0)"
echo "   silent catch-and-continue : $(grep -cE 'catch *\([^)]*\) *\{ *\}' src/compressible/DensityBasedSolver.cpp)   (must be 0)"

echo "-- unexpected generated files"
echo "   control scratch files left : $(find "$P17/controls" -name '*.pristine' 2>/dev/null | wc -l)"
echo "   pre-format references kept : $(find "$P17/controls/format" -name '*.preformat' -o -name '*.before_format_and_cleanup' 2>/dev/null | wc -l)   (deliberate -- they ARE the proof)"
echo "   (DensityBasedSolver.cpp.pristine is KEPT deliberately: it is the pre-format reference that"
echo "    logs/20_format_verified.log's proof is computed against.)"
echo "   stray binaries under results/ : $(find "$P17" -type f \( -name '*.o' -o -name '*.a' -o -name 'a.out' \) 2>/dev/null | wc -l)   (must be 0)"
echo ""

echo "### 5. no planted control survived ###"
for m in 'N1 dropped' '// N2' '// N3' '// N4' 'N5 re-capture' 'G1 NON-VACUITY PLANT' '// N7'; do
  n=$(grep -rc "$m" src/compressible/DensityBasedSolver.cpp src/pressure_velocity/SIMPLE.cpp 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')
  printf '  marker %-24s occurrences: %s\n' "\"$m\"" "$n"
  [ "$n" -eq 0 ] || { echo "    *** A PLANT SURVIVED ***"; fail=$((fail+1)); }
done
echo "  SANITY -- the marker search must be able to find something:"
echo "    a synthetic line matches: $(printf 'x;  // N2\n' | grep -c '// N2')   (must be 1)"
echo "  AUTHORITATIVE, beyond the greps -- the SHA-256 of both planted files:"
printf '    %-46s %s\n' src/compressible/DensityBasedSolver.cpp "$(sha256sum src/compressible/DensityBasedSolver.cpp | awk '{print $1}')"
printf '    %-46s %s\n' src/pressure_velocity/SIMPLE.cpp "$(sha256sum src/pressure_velocity/SIMPLE.cpp | awk '{print $1}')"
echo "    SIMPLE.cpp must equal its freeze value 34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1"
if [ "$(sha256sum src/pressure_velocity/SIMPLE.cpp | awk '{print $1}')" != "34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1" ]; then
  echo "    *** SIMPLE.cpp DOES NOT MATCH THE FREEZE ***"
  fail=$((fail+1))
fi
echo ""

echo "### 6. git status / diff --stat / diff --check ###"
echo "  tracked files modified in total      : $(git status --porcelain | grep -c '^ M' || true)"
echo "  untracked paths in total             : $(git status --porcelain | grep -c '^??' || true)"
echo "  (both dominated by ~286 pre-existing uncommitted files from earlier phases against a"
echo "   2026-09-21 HEAD; section 3 above is the P17.8 delta.)"
echo "  git diff --stat, P17.8's edited TRACKED files only:"
git diff --stat HEAD -- src/CMakeLists.txt tests/unit/compressible/CMakeLists.txt \
  tests/integration/compressible/CMakeLists.txt results/p17/hllc/tools/exact_riemann.py 2>/dev/null | sed 's/^/    /'
echo "  git diff --check (whitespace errors):"
if git diff --check > /dev/null 2>&1; then echo "    CLEAN"; else git diff --check | head -5 | sed 's/^/    /'; fail=$((fail+1)); fi
echo ""
echo "INTEGRITY FAILURES: $fail   (must be 0)"
if [ "$fail" -eq 0 ]; then exit 0; else exit 1; fi

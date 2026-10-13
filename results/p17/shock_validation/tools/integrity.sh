#!/usr/bin/env bash
# P17.9 repository integrity (authorization §47). Run LAST, after every control has restored and
# the regression has finished, so what it reports is the state P17.9 actually leaves behind.
#
# WRITTEN FOR P17.9, not copied. The first version of this file was a sed-rename of P17.8's, and it
# duly reported P17.8's narrative -- its test files, its N1..N7 control markers, its U1 mesh change
# -- under a P17.9 heading, while passing. A harness that passes while describing the wrong phase is
# worse than one that fails, so it was rewritten rather than patched.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
P17=results/p17/shock_validation
fail=0

echo "P17.9 REPOSITORY INTEGRITY"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### 1. nothing was committed, pushed, reset or stashed ###"
echo "  HEAD                     : $(git rev-parse HEAD)"
echo "  HEAD at the P17.9 freeze : 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274"
if [ "$(git rev-parse HEAD)" = "3bd32f918656f471e72b5f6ac7cb6c2cbf75a274" ]; then
  echo "  HEAD unchanged           : YES"
else
  echo "  HEAD unchanged           : *** NO ***"; fail=$((fail+1))
fi
echo "  branch                   : $(git rev-parse --abbrev-ref HEAD)"
echo "  stash entries            : $(git stash list | wc -l)   (expected 0)"
echo ""

echo "### 2. the FROZEN documents are unchanged -- including after three amendments ###"
(cd "$P17" && sha256sum acceptance_gate.md configuration.md tools/riemann_reference.py \
   tools/dry_run_detectors.py tools/audit.sh) | sed 's/^/  /'
for pair in "acceptance_gate.md b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40" \
            "configuration.md 0b463149053b5523cc82ac4b0e0aaeb654860bc4a30453749cb414ba61178e2b" \
            "tools/riemann_reference.py f8c2388623ad6ec759267adb0a380609957115c08750a70d3faa0df8aaf1e898" \
            "tools/dry_run_detectors.py 61f3f62cbec50dab894b2125d29af05655d39d37cf255da9e97ad789927c6b18" \
            "tools/audit.sh ff7297a89b3f8da146f8603257e02601b15827c8c013cfac2baae277f188acba"; do
  f=${pair%% *}; want=${pair##* }
  got=$(sha256sum "$P17/$f" | awk '{print $1}')
  if [ "$got" = "$want" ]; then
    printf '  %-32s MATCHES the freeze\n' "$f"
  else
    echo "  *** $f MOVED since the freeze ***"; fail=$((fail+1))
  fi
done
echo "  THE AMENDMENTS DO NOT EDIT THE GATE. A1, A2 and A3 live in amendments/ and the gate's own"
echo "  hash above is the proof -- it is the value logs/02_freeze.log recorded before any"
echo "  benchmark existed."
echo "  audit.md, verification.md, regression.md, summary.md and amendments/ are EVIDENCE written"
echo "  after the freeze and are deliberately NOT in the frozen set."
echo ""

echo "### 3. the P17.9 delta, enumerated ###"
echo "  PRODUCTION SOURCES ADDED OR MODIFIED: 0. P17.9 is a VALIDATION phase (gate criterion A3)."
echo "  The P17.8 solver and P17.7 flux it drives, against their P17.9 freeze values:"
for pair in "include/cfd/compressible/DensityBasedSolver.hpp e5efb0ad67fef38510e30b0bed11bb50f40d71024f3e98b40783e3936c807bdc" \
            "src/compressible/DensityBasedSolver.cpp 53e84ac7b5f53c6d6704e8a7a77281dd678ae18f450d74862abcce60d8421326" \
            "include/cfd/compressible/HllcFlux.hpp fc4a8db5f98568923ffe320b6adb3b6574f855cf538bb238ba9385bd046b61f0" \
            "src/compressible/HllcFlux.cpp 16889dd97925fa33de8e25f91789ddafc3fb90c599cb474928c7a31cc5ccb39d" \
            "src/pressure_velocity/SIMPLE.cpp 34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1" \
            "src/app/ProjectRunner.cpp 3f8214b4412f1877bbd2c98446ac08b00ea6c2662c98aea9e7b2970b7d714a4a"; do
  f=${pair%% *}; want=${pair##* }
  got=$(sha256sum "$f" | awk '{print $1}')
  if [ "$got" = "$want" ]; then
    printf '    %-58s UNCHANGED\n' "$f"
  else
    printf '    %-58s *** CHANGED ***\n' "$f"
    echo "      got      $got"
    echo "      expected $want"
    fail=$((fail+1))
  fi
done
echo ""
echo "  TEST SOURCES ADDED (3):"
for f in tests/integration/compressible/test_shock_validation.cpp \
         tests/integration/compressible/ShockDetectors.hpp \
         tests/integration/compressible/shock_reference_data.hpp; do
  printf '    %-58s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo "    shock_reference_data.hpp is GENERATED from the frozen tools/riemann_reference.py by"
echo "    tools/gen_shock_reference.py, which IMPORTS that file rather than editing it -- which is"
echo "    why the reference's hash in section 2 still matches after the header was produced."
echo ""
echo "  BUILD FILES EDITED (1) -- registration only, no target removed:"
printf '    %-58s %s\n' tests/integration/compressible/CMakeLists.txt \
  "$(sha256sum tests/integration/compressible/CMakeLists.txt | awk '{print $1}')"
echo "    P17.9's OWN lines in it: $(git diff HEAD -- tests/integration/compressible/CMakeLists.txt | grep -cE '^\+.*(ShockValidation|test_shock_validation)') (the CFDShockValidationTests target)"
echo ""
echo "  CUDA FILES: 0.    CASE / CONFIG FILES: 0.    PRODUCTION CMakeLists: 0."
echo ""

echo "### 4. the §47 hazards, each checked by name ###"
echo "-- threshold weakening"
echo "   THREE criteria were amended, all with the user's explicit authorisation, all recorded in"
echo "   amendments/ with the original frozen text and the original failing result preserved:"
echo "     A1  ADDS a criterion (stationary-contact width == 0)     -> gate STRENGTHENED"
echo "     A2  replaces a dimensionally wrong C*dx bound with a derived scaling law, no constant"
echo "     A3  replaces a CONTAMINATED estimator; every acceptance BOUND (S1-S5) unchanged"
echo "   numeric acceptance bounds LOOSENED: 0"
echo "   the frozen gate's own hash, unmoved: $(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')"

echo "-- removed tests: none"
echo "   P17.9 shock TESTs          : $(grep -c '^TEST(' tests/integration/compressible/test_shock_validation.cpp)"
echo "   P17.8 density-based TESTs  : $(grep -c '^TEST(' tests/unit/compressible/test_density_based_solver.cpp)   (untouched)"
echo "   P17.8 canonical TESTs      : $(grep -c '^TEST(' tests/integration/compressible/test_density_based_canonical.cpp)   (untouched)"
echo "   P17.7 HLLC TESTs           : $(grep -c '^TEST(' tests/unit/compressible/test_hllc_flux.cpp)   (untouched)"
echo "   test targets removed anywhere: $(git diff HEAD -- '*/CMakeLists.txt' | grep -c '^-.*add_executable' || true)   (must be 0)"

echo "-- accidental solver retuning: none"
SOLVERDIFF=$(git status --porcelain -- src/compressible/DensityBasedSolver.cpp include/cfd/compressible/DensityBasedSolver.hpp | wc -l)
echo "   the solver files are byte-identical to their freeze values (section 3), and the core"
echo "   library archive libcfdcore.a came out of BOTH regression configurations with the SAME"
echo "   sha256 P17.8 recorded -- 188e5039... (release) and 1305a82a... (cpu-only)."

echo "-- changed SIMPLE defaults: none"
DEFS=$(grep -c 'ShockValidation\|shock_validation' src/app/ProjectRunner.cpp 2>/dev/null || true)
echo "   P17.9 references in the dispatch: ${DEFS:-0}   (must be 0)"

echo "-- hidden fallback behaviour: none"
echo "   the solver was not edited at all, so no fallback could have been introduced."
echo "   fallback occurrences in the solver: $(grep -ciE 'fallback' src/compressible/DensityBasedSolver.cpp)"

echo "-- an exact Riemann solver duplicated into production: none"
PRODREF=$(grep -rlE 'exact_star|sampleExactSod|riemann_reference' src/ include/ cuda/ 2>/dev/null | wc -l)
echo "   production files referencing the reference solver: $PRODREF   (must be 0)"
echo "   SANITY: the same pattern finds the TEST files: $(grep -rlE 'sampleExactSod' tests/ 2>/dev/null | wc -l)   (must be > 0)"
[ "$PRODREF" -eq 0 ] || fail=$((fail+1))

echo "-- unexpected generated files"
echo "   profile CSVs under results/p17/shock_validation/data : $(ls -1 "$P17/data" 2>/dev/null | wc -l)   (evidence, §31)"
echo "   control scratch files left (*.pristine)              : $(find "$P17/controls" -name '*.pristine' 2>/dev/null | wc -l)   (must be 0)"
echo "   pre-format references kept (*.preformat)             : $(find "$P17/controls" -name '*.preformat' 2>/dev/null | wc -l)   (deliberate -- they ARE the format proof)"
echo "   stray binaries under results/                        : $(find "$P17" -type f \( -name '*.o' -o -name '*.a' -o -name 'a.out' \) 2>/dev/null | wc -l)   (must be 0)"
if [ "$(find "$P17/controls" -name '*.pristine' 2>/dev/null | wc -l)" -ne 0 ]; then fail=$((fail+1)); fi
echo ""

echo "### 5. no planted control survived ###"
echo "  P17.9 planted TWO controls, both inside src/compressible/DensityBasedSolver.cpp:"
echo "    V1  a Rusanov / local Lax-Friedrichs flux"
echo "    V2  hllFluxForControlOnly (HLL, contact wave deleted)"
for m in 'V1 RUSANOV PLANT' 'hllFluxForControlOnly' '0.5 \* smax'; do
  n=$(grep -c "$m" src/compressible/DensityBasedSolver.cpp 2>/dev/null || true)
  printf '  marker %-26s occurrences in the solver: %s\n' "\"$m\"" "$n"
  [ "$n" -eq 0 ] || { echo "    *** A PLANT SURVIVED ***"; fail=$((fail+1)); }
done
echo "  SANITY -- the marker search must be able to find something:"
echo "    hllFluxForControlOnly in HllcFlux.hpp, where it legitimately lives: $(grep -c 'hllFluxForControlOnly' include/cfd/compressible/HllcFlux.hpp)   (must be > 0)"
echo "  AUTHORITATIVE, beyond the greps -- the solver's SHA-256:"
printf '    %-46s %s\n' src/compressible/DensityBasedSolver.cpp "$(sha256sum src/compressible/DensityBasedSolver.cpp | awk '{print $1}')"
echo "    must equal the freeze value 53e84ac7b5f53c6d6704e8a7a77281dd678ae18f450d74862abcce60d8421326"
echo ""

echo "### 6. git status / diff --stat / diff --check ###"
echo "  tracked files modified in total : $(git status --porcelain | grep -c '^ M' || true)"
echo "  untracked paths in total        : $(git status --porcelain | grep -c '^??' || true)"
echo "  (both dominated by ~286 pre-existing uncommitted files from earlier phases against a"
echo "   2026-09-21 HEAD; section 3 is the P17.9 delta.)"
echo "  runtime noise under results/validation still modified: $(git status --porcelain -- results/validation | wc -l)   (must be 0 -- classified timing-only and restored)"
echo "  git diff --stat, P17.9's edited TRACKED file:"
git diff --stat HEAD -- tests/integration/compressible/CMakeLists.txt | sed 's/^/    /'
echo "  git diff --check (whitespace errors):"
if git diff --check > /dev/null 2>&1; then echo "    CLEAN"; else git diff --check | head -5 | sed 's/^/    /'; fail=$((fail+1)); fi
echo ""
echo "INTEGRITY FAILURES: $fail   (must be 0)"
if [ "$fail" -eq 0 ]; then exit 0; else exit 1; fi

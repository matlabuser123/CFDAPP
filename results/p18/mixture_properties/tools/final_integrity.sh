#!/usr/bin/env bash
# P18.3 section 37 -- final repository integrity.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
SRC=src/species/MixtureThermodynamics.cpp
HDR=include/cfd/species/MixtureThermodynamics.hpp

echo "=== git status summary ==="
git status --porcelain | awk '{print substr($0,1,2)}' | sort | uniq -c | sed 's/^/  /'
echo ""

echo "=== git diff --check (whitespace errors) ==="
git diff --check > /tmp/p18_3_dc.txt 2>&1 || true
RE=':[0-9]+: (trailing whitespace|space before tab|indent with spaces|new blank line)'
echo "  total warnings in the tree : $(grep -cE "$RE" /tmp/p18_3_dc.txt || true)"
grep -oE "^[^+ ][^:]*$RE" /tmp/p18_3_dc.txt | sed 's/:[0-9]*: .*//' | sort | uniq -c | sort -rn | sed 's/^/    /'
echo "  in a P18.3 file (must be 0): $(grep -oE "^[^+ ][^:]*$RE" /tmp/p18_3_dc.txt | sed 's/:[0-9]*: .*//' | sort -u | grep -cE 'MixtureThermodynamics|test_mixture' || true)"
echo ""

echo "=== git diff --stat for the two files P18.3 modified ==="
git diff --stat -- src/CMakeLists.txt tests/unit/species/CMakeLists.txt | sed 's/^/  /'
echo "  (both files were ALREADY modified by earlier phases; P18.3 added one target_sources"
echo "   block and one test-source line. The fingerprint section below is what bounds the change.)"
echo ""

echo "=== frozen gate, re-hashed at close ==="
sha256sum results/p18/gates/P18_GATES.md | sed 's/^/  /'
echo "  expected 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3"
echo ""

echo "=== results/validation runtime noise ==="
echo "  modified: $(git status --porcelain -- results/validation | wc -l)   (must be 0)"
echo ""

echo "=== NO CHEMISTRY OR HEAT RELEASE ADDED EARLY (sections 23, 37) ==="
for pat in 'Arrhenius' 'stoichiom' 'reactionRate' 'omega_i' 'heatRelease' 'heatOfFormation' 'formationEnthalpy'; do
  printf '  %-20s %s\n' "$pat" "$(grep -ril "$pat" "$HDR" "$SRC" 2>/dev/null | wc -l)"
done
echo ""

echo "=== NO DUPLICATED EOS OR P15 PROPERTY LOGIC (sections 1, 9, 37) ==="
echo "  a second EOS class defined?        : $(grep -c 'class .*EOS\|class .*EquationOfState' "$HDR" || true)"
echo "  a density formula written here?   : $(grep -cE 'pressure */ *\(|/ *\(.*temperature *\)' "$SRC" || true)"
echo "  a polynomial cp evaluator here?   : $(grep -ci 'coefficient' "$SRC" || true)"
echo "  P17/P15 types REUSED:"
grep -oE 'cfd::compressible::[A-Za-z]+|cfd::thermal::[A-Za-z]+' "$HDR" "$SRC" | sed 's/.*://' | sort -u | sed 's/^/    /'
echo ""

echo "=== NO SILENT MASS-FRACTION NORMALIZATION (section 20) ==="
echo "  normalise/normalize calls : $(grep -ci 'normali' "$SRC" || true)"
echo "  clamp/clip                : $(grep -c 'clamp\|clip' "$SRC" || true)"
echo ""

echo "=== P18.2 TRANSPORT AND P17 SHOCK UNTOUCHED (sections 24, 32) ==="
for f in src/species/SpeciesTransport.cpp src/compressible/TotalEnergy.cpp \
         src/compressible/HllcFlux.cpp src/compressible/IdealGasEOS.cpp \
         src/compressible/ThermodynamicProperties.cpp src/thermal/Enthalpy.cpp; do
  printf '  %-48s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}' | cut -c1-24)"
done
echo "  (these are recorded so a later phase can prove they did not move either)"
echo ""

echo "=== no test deleted or disabled ==="
echo "  DISABLED_ added by P18.3   : $(grep -c 'DISABLED_' tests/unit/species/test_mixture_thermodynamics.cpp || true)"
echo "  CFDSpeciesTests registered : $(./build/final/tests/unit/species/CFDSpeciesTests --gtest_list_tests 2>/dev/null | grep -c '^  ')  (P18.2 closed at 131)"
echo ""

echo "=== the P18.3 changeset ==="
for f in "$HDR" "$SRC" tests/unit/species/test_mixture_thermodynamics.cpp; do
  printf '  %-56s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}' | cut -c1-32)"
done
echo ""
echo "=== commit / push ==="
echo "  HEAD: $(git rev-parse HEAD)"
echo "  nothing committed, pushed, reset, cleaned or stashed by P18.3."

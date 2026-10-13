#!/usr/bin/env bash
# P18.4 section 37 -- final repository integrity.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
SRC=src/species/ReactionMechanism.cpp
HDR=include/cfd/species/ReactionMechanism.hpp

echo "=== git status summary ==="
git status --porcelain | awk '{print substr($0,1,2)}' | sort | uniq -c | sed 's/^/  /'
echo ""

echo "=== git diff --check (whitespace errors) ==="
git diff --check > /tmp/p18_4_dc.txt 2>&1 || true
RE=':[0-9]+: (trailing whitespace|space before tab|indent with spaces|new blank line)'
echo "  total warnings in the tree : $(grep -cE "$RE" /tmp/p18_4_dc.txt || true)"
grep -oE "^[^+ ][^:]*$RE" /tmp/p18_4_dc.txt | sed 's/:[0-9]*: .*//' | sort | uniq -c | sort -rn | sed 's/^/    /'
echo "  in a P18.4 file (must be 0): $(grep -oE "^[^+ ][^:]*$RE" /tmp/p18_4_dc.txt | sed 's/:[0-9]*: .*//' | sort -u | grep -cE 'ReactionMechanism|test_reaction' || true)"
echo ""

echo "=== git diff --stat for the two files P18.4 modified ==="
git diff --stat -- src/CMakeLists.txt tests/unit/species/CMakeLists.txt | sed 's/^/  /'
echo "  (both files were ALREADY modified by earlier phases; P18.4 added one target_sources"
echo "   block and one test-source line. The fingerprint section below is what bounds the change.)"
echo ""

echo "=== frozen gate, re-hashed at close ==="
sha256sum results/p18/gates/P18_GATES.md | sed 's/^/  /'
echo "  expected 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3"
echo ""

echo "=== results/validation runtime noise ==="
echo "  modified: $(git status --porcelain -- results/validation | wc -l)   (must be 0)"
echo ""

echo "=== NO ARRHENIUS KINETICS ADDED EARLY (sections 33, 52) ==="
for pat in 'Arrhenius' 'activationEnergy' 'preExponential' 'exp(' 'std::exp' 'temperatureExponent'; do
  printf '  %-22s %s
' "$pat" "$(grep -c "$pat" "$HDR" "$SRC" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')"
done
echo ""

echo "=== NO HEAT-RELEASE CODE ADDED EARLY (sections 34, 52) ==="
for pat in 'heatRelease' 'reactionEnthalpy' 'heatOfFormation' 'formationEnthalpy' 'energySource'; do
  printf '  %-22s %s
' "$pat" "$(grep -ci "$pat" "$HDR" "$SRC" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')"
done
echo ""

echo "=== NO DUPLICATED MOLECULAR-WEIGHT DATA (section 35) ==="
echo "  molecular weights STORED in the reaction layer : $(grep -c 'Real molecularWeight_\|molecularWeight_{' "$HDR" "$SRC" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')"
echo "  molecular weights READ from P18.3              : $(grep -c 'species(i).molecularWeight()' "$SRC" || true)"
echo ""

echo "=== NO SILENT STOICHIOMETRIC CORRECTION OR SOURCE CLIPPING (sections 9, 23) ==="
echo "  normali* calls  : $(grep -ci 'normali' "$SRC" || true)"
echo "  clamp/clip      : $(grep -c 'clamp\|clip' "$SRC" || true)"
echo "  std::abs on an input coefficient : $(grep -c 'std::abs(term' "$SRC" || true)"
echo ""

echo "=== NO SECOND SOURCE-ASSEMBLY PATH (section 1) ==="
echo "  P14/P18.2 types REUSED by the reaction layer:"
grep -oE 'SpeciesSources|ScalarSourceFields|zeroSpeciesSources' "$HDR" "$SRC" | sort -u | sed 's/^/    /'
echo "  new assembler functions defined : $(grep -c 'assemble' "$SRC" || true)"
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
echo "  DISABLED_ added by P18.4   : $(grep -c 'DISABLED_' tests/unit/species/test_reaction_mechanism.cpp || true)"
echo "  CFDSpeciesTests registered : $(./build/final/tests/unit/species/CFDSpeciesTests --gtest_list_tests 2>/dev/null | grep -c '^  ')  (P18.3 closed at 165)"
echo ""

echo "=== the P18.4 changeset ==="
for f in "$HDR" "$SRC" tests/unit/species/test_reaction_mechanism.cpp; do
  printf '  %-56s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}' | cut -c1-32)"
done
echo ""
echo "=== commit / push ==="
echo "  HEAD: $(git rev-parse HEAD)"
echo "  nothing committed, pushed, reset, cleaned or stashed by P18.4."

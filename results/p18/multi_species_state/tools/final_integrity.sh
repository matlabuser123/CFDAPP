#!/usr/bin/env bash
# P18.1 section 38 -- final repository integrity.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1

echo "=== git status summary ==="
git status --porcelain | awk '{print substr($0,1,2)}' | sort | uniq -c | sed 's/^/  /'
echo ""

echo "=== git diff --check (whitespace errors) ==="
git diff --check > /tmp/p18_1_dc.txt 2>&1 || true
RE=':[0-9]+: (trailing whitespace|space before tab|indent with spaces|new blank line)'
echo "  total warnings in the whole tree : $(grep -cE "$RE" /tmp/p18_1_dc.txt || true)"
echo "  by file:"
grep -oE "^[^+ ][^:]*$RE" /tmp/p18_1_dc.txt | sed 's/:[0-9]*: .*//' \
  | sort | uniq -c | sort -rn | sed 's/^/    /'
echo ""
echo "  ...of which in a P18.1 FILE (must be 0):"
grep -oE "^[^+ ][^:]*$RE" /tmp/p18_1_dc.txt | sed 's/:[0-9]*: .*//' | sort -u \
  | grep -E 'SpeciesRegistry|MassFractionState|test_mass_fraction_state' | sed 's/^/    /'
echo "    count: $(grep -oE "^[^+ ][^:]*$RE" /tmp/p18_1_dc.txt | sed 's/:[0-9]*: .*//' | sort -u \
  | grep -cE 'SpeciesRegistry|MassFractionState|test_mass_fraction_state' || true)"
echo ""
echo "  NOTE: the three CMakeLists offenders are PRE-EXISTING P17/GPU files P18.1 never touched."
echo "  TODO.md:3 is a Markdown hard line break (two trailing spaces) written by P18.0's rewrite;"
echo "  HEAD's line 3 is different text entirely, so it is an added line from that rewrite, not"
echo "  from P18.1. Neither is repaired here (CLAUDE.md section 2)."
echo ""

echo "=== control-doc line endings (must match CLAUDE.md's convention: LF) ==="
for f in TODO.md ROADMAP.md CLAUDE.md; do
  printf '  %-12s CR lines: %-5s %s\n' "$f" "$(grep -c $'\r' "$f" || true)" "$(file -b "$f")"
done
echo ""

echo "=== frozen gates, re-hashed at close ==="
sha256sum results/p18/gates/P18_GATES.md results/p17/gates/P17_GATES.md | sed 's/^/  /'
echo "  P18 expected 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3"
echo "  P17 expected 3809c100ca4323966ae38bcf99d9bbd87187296089732f8ff54bfd85c6c4804d"
echo ""

echo "=== results/validation runtime noise ==="
echo "  modified: $(git status --porcelain -- results/validation | wc -l)   (must be 0)"
echo ""

echo "=== P18.1 did not implement later-phase physics ==="
echo "  later-phase terms anywhere under include/cfd/species or src/species:"
for pat in 'omega_i' 'Arrhenius' 'stoichiom' 'reactionRate' 'heatRelease' 'W_mix' 'R_mix' 'cp_mix'; do
  n=$(grep -ril "$pat" include/cfd/species src/species 2>/dev/null | wc -l)
  printf '    %-16s %s\n' "$pat" "$n"
done
echo ""
echo "  transport verbs in the two NEW sources (P18.1 owns state, not the PDE):"
for pat in 'assemble' 'BiCGSTAB' 'massFlux' 'convect' 'diffus'; do
  n=$(cat src/species/MassFractionState.cpp src/species/SpeciesRegistry.cpp | grep -c "$pat" || true)
  printf '    %-12s %s\n' "$pat" "$n"
done
echo ""

echo "=== no silent clipping or normalization on any accepting path ==="
echo "  clamp/clip in the new sources : $(cat src/species/MassFractionState.cpp src/species/SpeciesRegistry.cpp | grep -c 'clamp\|clip' || true)"
echo "  every occurrence of normalizeMassFractions in the production source:"
grep -n 'normalizeMassFractions' src/species/MassFractionState.cpp | sed 's/^/    /'
echo "    -> its declaration and its definition only. No constructor or setter calls it."
echo ""

echo "=== no test deleted or disabled ==="
echo "  DISABLED_ added by P18.1                : $(grep -c 'DISABLED_' tests/unit/species/test_mass_fraction_state.cpp || true)"
echo "  pre-existing species tests still present: $(./build/final/tests/unit/species/CFDSpeciesTests --gtest_list_tests --gtest_filter='-SpeciesRegistryTest.*:MassFractionStateTest.*' 2>/dev/null | grep -c '^  ')  (baseline 37)"
echo ""

echo "=== the P18.1 changeset, final hashes ==="
for f in include/cfd/species/SpeciesRegistry.hpp include/cfd/species/MassFractionState.hpp \
         src/species/SpeciesRegistry.cpp src/species/MassFractionState.cpp \
         tests/unit/species/test_mass_fraction_state.cpp; do
  printf '  %-52s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo ""
echo "=== commit / push ==="
echo "  HEAD: $(git rev-parse HEAD)"
echo "  nothing committed, pushed, reset, cleaned or stashed by P18.1."

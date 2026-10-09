#!/usr/bin/env bash
# P18.2 section 45 -- final repository integrity.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
SRC=src/species/SpeciesTransport.cpp

echo "=== git status summary ==="
git status --porcelain | awk '{print substr($0,1,2)}' | sort | uniq -c | sed 's/^/  /'
echo ""

echo "=== git diff --check ==="
git diff --check > /tmp/p18_2_dc.txt 2>&1 || true
RE=':[0-9]+: (trailing whitespace|space before tab|indent with spaces|new blank line)'
echo "  total warnings in the tree : $(grep -cE "$RE" /tmp/p18_2_dc.txt || true)"
grep -oE "^[^+ ][^:]*$RE" /tmp/p18_2_dc.txt | sed 's/:[0-9]*: .*//' | sort | uniq -c | sort -rn | sed 's/^/    /'
echo "  in a P18.2 file (must be 0): $(grep -oE "^[^+ ][^:]*$RE" /tmp/p18_2_dc.txt | sed 's/:[0-9]*: .*//' | sort -u | grep -cE 'SpeciesTransport|ScalarTransport|test_species_transport' || true)"
echo ""

echo "=== frozen documents, re-hashed at close ==="
sha256sum results/p18/gates/P18_GATES.md results/p18/species_transport/diffusion_closure.md \
          results/p18/species_transport/formulation.md results/p18/species_transport/audit.md \
  | sed 's/^/  /'
echo "  P18_GATES expected      921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3"
echo "  diffusion_closure.md is AMENDED BY A1, NOT EDITED -- its hash must be the one A1 recorded."
echo ""

echo "=== results/validation runtime noise ==="
echo "  modified: $(git status --porcelain -- results/validation | wc -l)   (must be 0)"
echo ""

echo "=== no later-phase physics added (section 45) ==="
for pat in 'Arrhenius' 'stoichiom' 'reactionRate' 'heatRelease' 'W_mix' 'R_mix' 'cp_mix' 'gamma_mix' 'omega_i ='; do
  printf '  %-16s %s\n' "$pat" "$(grep -ril "$pat" include/cfd/species src/species 2>/dev/null | wc -l)"
done
echo ""

echo "=== no duplicate P14 assemblers (section 1) ==="
echo "  SpeciesConvectionAssembler : $(grep -ril 'SpeciesConvectionAssembler' include src 2>/dev/null | wc -l)"
echo "  SpeciesDiffusionAssembler  : $(grep -ril 'SpeciesDiffusionAssembler' include src 2>/dev/null | wc -l)"
echo "  P14 assemblers CALLED from the species transport:"
grep -oE 'cfd::transport::assemble[A-Za-z]+|cfd::transport::apply[A-Za-z]+' "$SRC" | sort -u | sed 's/^/    /'
echo ""

echo "=== the mass flux is reused, never recomputed (section 3) ==="
echo "  flux-forming calls in the species transport (must be 0): $(grep -cE 'calculateMassFlux|rho_f|faceVelocity' "$SRC" || true)"
echo ""

echo "=== no silent clipping or normalization (sections 7, 8) ==="
echo "  clamp/clip                      : $(grep -c 'clamp\|clip' "$SRC" || true)"
echo "  normalizeMassFractions called   : $(grep -c 'normalizeMassFractions' "$SRC" || true)"
echo ""

echo "=== no test deleted or disabled ==="
echo "  DISABLED_ added by P18.2        : $(grep -c 'DISABLED_' tests/unit/species/test_species_transport.cpp || true)"
echo "  CFDSpeciesTests registered      : $(./build/final/tests/unit/species/CFDSpeciesTests --gtest_list_tests 2>/dev/null | grep -c '^  ')  (P18.1 closed at 97)"
echo ""

echo "=== the P18.2 changeset ==="
for f in include/cfd/species/SpeciesTransport.hpp src/species/SpeciesTransport.cpp \
         include/cfd/transport/ScalarTransport.hpp src/transport/ScalarTransport.cpp \
         include/cfd/species/MassFractionState.hpp tests/unit/species/test_species_transport.cpp; do
  printf '  %-52s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}' | cut -c1-32)"
done
echo ""
echo "=== commit / push ==="
echo "  HEAD: $(git rev-parse HEAD)"
echo "  nothing committed, pushed, reset, cleaned or stashed by P18.2."

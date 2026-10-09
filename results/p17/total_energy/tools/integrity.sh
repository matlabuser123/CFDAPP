#!/usr/bin/env bash
# P17.5 repository integrity. Run LAST, after every control has restored and the regression has
# finished, so what it reports is the state P17.5 actually leaves behind.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/total_energy
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"

DOCEXC=(. ':(exclude)results/' ':(exclude)TODO.md' ':(exclude)ROADMAP.md' ':(exclude)CLAUDE.md'
        ':(exclude)README.md' ':(exclude)CONTRIBUTING.md' ':(exclude)QUALITY_GATE.md'
        ':(exclude)PROJECT_STRUCTURE.md' ':(exclude)RELEASE_NOTES_*.md' ':(exclude)docs/')

code_fp() {
  local T U
  T=$(git diff --binary HEAD -- "${DOCEXC[@]}" | sha256sum | awk '{print $1}')
  U=$(git ls-files --others --exclude-standard -- "${DOCEXC[@]}" \
      | LC_ALL=C sort | while read -r f; do sha256sum "$f"; done | sha256sum | awk '{print $1}')
  printf '%s %s' "$T" "$U" | sha256sum | awk '{print $1}'
}

log=$P17/logs/20_integrity.log
{
echo "P17.5 REPOSITORY INTEGRITY"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### 1. nothing was committed, pushed, reset or stashed ###"
echo "  HEAD                     : $(git rev-parse HEAD)"
echo "  HEAD at the P17.5 freeze : 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274"
echo "  HEAD unchanged           : $([ "$(git rev-parse HEAD)" = "3bd32f918656f471e72b5f6ac7cb6c2cbf75a274" ] && echo YES || echo '*** NO ***')"
echo "  branch                   : $(git rev-parse --abbrev-ref HEAD)"
echo "  stash entries            : $(git stash list | wc -l)   (expected 0)"
echo ""

echo "### 2. the frozen documents are unchanged ###"
sha256sum "$P17/acceptance_gate.md" "$P17/audit.md" "$P17/thermodynamic_contract.md" \
  | sed "s|$P17/||" | sed 's/^/  /'
echo "  expected, from logs/02_freeze.log:"
echo "    ca84e432a277a2724f0c94a8ccc46294ff405b5b8168b66327690a01ba72a04b  acceptance_gate.md"
echo "    d230209e9a903fed353467da0d6447acaade83f84d31860d65516eda336dc63d  audit.md"
echo "    cb999b044aae20dd8758c35331be9d2867bf7c0538596c2e40c1298681c8b47e  thermodynamic_contract.md"
echo ""

echo "### 3. upstream files UNTOUCHED -- SHA-256, never git diff HEAD ###"
untouched=0
touched=0
check() {
  local got
  got=$(sha256sum "$2" | awk '{print $1}')
  if [ "$got" = "$1" ]; then
    untouched=$((untouched + 1))
  else
    touched=$((touched + 1))
    echo "  *** TOUCHED: $2  (got $got, expected $1)"
  fi
}
check 6fa846217f09cafb982cbe01690e8e0aa6a61f35105babc3894f4d6e0ac5876e include/cfd/thermal/EnergyEquation.hpp
check 910214817717b6e8f95ff7c0f8c0294c875b9c316244320a4cdb04a24db2b3e5 src/thermal/EnergyEquation.cpp
check 9e7e758fba65f7c22e91f1624df8d4d71f8c4d818da4a295f5e43a0485fb1dee src/thermal/Enthalpy.cpp
check f429b79bc9b8bf2c2ed711946f317ea58730beb6cc6e1042a35d2bb37b4c19bd include/cfd/thermal/ThermalProperties.hpp
check e72f53ca3c83324377d94a8b67997f416e26bc810d91e981dc4606581d98721e include/cfd/thermal/TemperatureDependentProperty.hpp
check bdd096490437ee602de31695184035e2ce28f0ab4d2da6187e45c709e919324e include/cfd/compressible/IdealGasEOS.hpp
check daa27c5fc7e296482fd29fd93a0b4b6185fc7f3d39bc72855aa6240aad3bfe24 src/compressible/IdealGasEOS.cpp
check 1dd39383f5d87eb77a5b69252737db0cd0640121ea6e19eb5ec3916377008335 include/cfd/compressible/ThermodynamicProperties.hpp
check 27d74d9e535f8e716421ce24e7f955f32dc8200c5ae5a3ddfd1c9cbdb4da3a88 src/compressible/ThermodynamicProperties.cpp
check d376375ce2308b2ffe16968d2a063bda595d70194de9d6eb8faa147e1c1a0ea3 include/cfd/compressible/CompressibleMassFlux.hpp
check 2864c6ebb8b85b35ffd32c88544bb427fa3d87cd573569c60b6b53a808d95ca7 src/compressible/CompressibleMassFlux.cpp
check 842b927fb63f9cff4306fa8d1f1c8bbc3e06088dffd8e9b88308513e253dcc5b include/cfd/compressible/DilatationalStress.hpp
check fc00192ddb9d024fa1c61fcf6d7911fb7b862ec811ddc503b10e18370bc0c5e3 src/compressible/DilatationalStress.cpp
check 7feca5c4a3a22943c442d1fa162b4e860ab037b69a3c6a6ce0e6c413bb0e37af include/cfd/physics/MomentumEquation.hpp
check e46d49fcda78e0271538bc69117e249b0ea50dd2265c5f603d64d4206dc7bb59 src/physics/MomentumEquation.cpp
echo "  UNTOUCHED: $untouched    TOUCHED: $touched   (TOUCHED must be 0)"
echo ""

echo "### 4. no planted control marker survived ###"
# The pattern is ANCHORED TO END OF LINE. The first version was `// N[0-9]` unanchored and
# reported 2 surviving markers that were pre-existing comments in src/discretization/Diffusion.cpp
# about Newton interpolation NODES named N1 and N2 ("fit a cubic through boundary, owner, N1, N2").
# Verified present at HEAD, so entirely unrelated to this phase. controls.sh always appends its
# marker as a trailing comment, so anchoring with $ excludes prose while still catching a real
# restore failure anywhere in the tree.
markers=$(grep -rnE '// N[0-9]b?$' include/ src/ cuda/ 2>/dev/null | wc -l)
echo "  surviving // N<id> markers in include/ src/ cuda/ : $markers   (expected 0)"
if [ "$markers" -ne 0 ]; then
  grep -rnE '// N[0-9]b?$' include/ src/ cuda/ 2>/dev/null | sed 's/^/    /'
fi
echo "  (unanchored '// N[0-9]' would report 2 -- pre-existing Newton-node comments in"
echo "   src/discretization/Diffusion.cpp, present at HEAD. A loose probe is not a finding.)"
echo "  sanity: the pattern DOES match a trailing marker:"
printf 'x;  // N3
' | grep -cE '// N[0-9]b?$' | sed 's/^/    matches on a synthetic marker line: /'
echo "  control scratch files left : $(find "$P17/controls" -name '*.pristine*' 2>/dev/null | wc -l)   (expected 0)"
echo ""

echo "### 5. clang-format across every file P17.5 touched ###"
clang-format-18 --dry-run --Werror \
  include/cfd/compressible/TotalEnergy.hpp \
  src/compressible/TotalEnergy.cpp \
  include/cfd/compressible/TotalEnergyEquation.hpp \
  src/compressible/TotalEnergyEquation.cpp \
  tests/unit/compressible/test_total_energy.cpp 2>&1 | head -8
echo "  exit: ${PIPESTATUS[0]}   (0 = clean)"
echo ""

echo "### 6. no duplicate EOS or conductivity logic was introduced ###"
echo "  new EOS implementations           : $(grep -rc 'p = rho \* R \* T\|density \* gasConstant \* temperature' src/compressible/TotalEnergy.cpp src/compressible/TotalEnergyEquation.cpp | awk -F: '{s+=$2} END{print s}')   (0 required -- pressure goes through IdealGasEOS)"
echo "  equationOfState().pressure() uses : $(grep -c 'equationOfState().pressure' src/compressible/TotalEnergy.cpp)"
echo "  new conductivity correlations     : $(grep -rc 'conductivity =.*temperature\|k0 +\|kCoeff' src/compressible/TotalEnergyEquation.cpp | awk -F: '{s+=$2} END{print s}')   (0 required -- k is an argument)"
echo "  P15 helper reuse in conduction    : $(grep -c 'boundaryDiffusionContribution\|internalFaceDiffusionTerms' src/compressible/TotalEnergyEquation.cpp)"
echo ""

echo "### 7. the code fingerprint ###"
echo "  P17_CODE_FINGERPRINT now              : $(code_fp)"
echo "  at the P17.4 close (the P17.5 freeze) : 4e253ce4ce955f629002a0e6102bb864ff564c1492d007ed7300fc05a019cc87"
echo "  NOTE: this MUST differ -- P17.5 added code. It is an identifier, not a pass/fail; its"
echo "        pass/fail use is that it must not move DURING a regression run."
echo ""

echo "### 8. files P17.5 introduced ###"
P17_5_NEW="include/cfd/compressible/TotalEnergy.hpp src/compressible/TotalEnergy.cpp
include/cfd/compressible/TotalEnergyEquation.hpp src/compressible/TotalEnergyEquation.cpp
tests/unit/compressible/test_total_energy.cpp"
for f in $P17_5_NEW; do
  if git ls-files --error-unmatch "$f" >/dev/null 2>&1; then
    echo "    $f  TRACKED (unexpected -- P17.5 committed nothing)"
  else
    echo "    $f  untracked, as expected for a new file"
  fi
done
all_untracked=$(git ls-files --others --exclude-standard -- . ':(exclude)results/' | wc -l)
echo "  P17.5 new files                : 5"
echo "  ALL untracked in the tree      : $all_untracked"
echo "  attributable to EARLIER phases : $(( all_untracked - 5 ))"
echo "  (the enumeration is explicit rather than a listing of every untracked file: nothing since"
echo "   P12 is committed, so the untracked set is the union of every uncommitted phase)"
echo ""

echo "### 9. the suite size ###"
tests_now=$(grep -c '^TEST(' tests/unit/compressible/test_total_energy.cpp)
echo "  TESTs in test_total_energy.cpp : $tests_now   (derived, not hard-coded)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

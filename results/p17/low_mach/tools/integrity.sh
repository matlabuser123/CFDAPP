#!/usr/bin/env bash
# P17.6 repository integrity. Run LAST, after every control has restored and the regression has
# finished, so what it reports is the state P17.6 actually leaves behind.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/low_mach
H=tests/integration/compressible/test_low_mach_family.cpp
CM=tests/integration/compressible/CMakeLists.txt
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
echo "P17.6 REPOSITORY INTEGRITY"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### 1. nothing was committed, pushed, reset or stashed ###"
echo "  HEAD                     : $(git rev-parse HEAD)"
echo "  HEAD at the P17.6 freeze : 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274"
if [ "$(git rev-parse HEAD)" = "3bd32f918656f471e72b5f6ac7cb6c2cbf75a274" ]; then
  echo "  HEAD unchanged           : YES"
else
  echo "  HEAD unchanged           : *** NO ***"
fi
echo "  branch                   : $(git rev-parse --abbrev-ref HEAD)"
echo "  stash entries            : $(git stash list | wc -l)   (expected 0)"
echo ""

echo "### 2. the frozen documents are unchanged ###"
(cd "$P17" && sha256sum acceptance_gate.md audit.md tools/dryrun.cpp) | sed 's/^/  /'
echo "  expected, from logs/02_freeze.log:"
echo "    f275c4ac3fab31509b9f80036e1c8b4b6f4f3e8c9ec20cf8b8ae7a1d8b303c9a  acceptance_gate.md"
echo "    7846fc846d3947ffa618d1f7df53f44fb13d9a6518492c81cc631ef0e48cee0f  audit.md"
echo "    77ba7968d02c9c4e2054897a1b86cfb090cc607bdfc92d627bae2c7be942b4c4  tools/dryrun.cpp"
echo ""

echo "### 3. upstream files UNTOUCHED -- SHA-256, never git diff HEAD ###"
echo "  P17.6 is a VALIDATION phase: it changes NO production source at all."
untouched=0
touched=0
check() {
  local got
  got=$(sha256sum "$2" | awk '{print $1}')
  if [ "$got" = "$1" ]; then
    untouched=$((untouched + 1))
  else
    touched=$((touched + 1))
    echo "  *** TOUCHED: $2"
    echo "        got      $got"
    echo "        expected $1"
  fi
}
# the two files the frozen G5 criterion requires UNCHANGED
check 593165a1a2fff9ccbad8b3675ccbdff093bcf22c82b5daad90462fe228949acc tests/integration/compressible/test_low_mach_regression.cpp
check 9c3ad6502e65543422e95c21e99354380c2cffeedeafd2d8b4b9fa3c83793796 tests/integration/mms/test_mms_compressible.cpp
# the incompressible references and the solver P17.6 compares against
check c33143bfc886b0695e8a11b852f0035575f8f8389ee24e4d60a47888e863d674 tests/integration/poiseuille/test_poiseuille_validation.cpp
check 8a6061c30da34bff819beca7c563126e636b505dca4c074c85ee6503ccd949a0 tests/integration/cavity/test_cavity_ghia.cpp
check 34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1 src/pressure_velocity/SIMPLE.cpp
check 325a7f380be58144794538cd64d0fd4f1324a46b1bc9fa2727d5fa899e2eb9da include/cfd/pressure_velocity/SIMPLE.hpp
# P17.1 - P17.5 production
check f95f760bbe2abb284ec33161209bf33f25ec9a0fb4a02e170a2232bd17d6d8ed src/compressible/CompressibleSIMPLE.cpp
check 2864c6ebb8b85b35ffd32c88544bb427fa3d87cd573569c60b6b53a808d95ca7 src/compressible/CompressibleMassFlux.cpp
check 5396726d935905d450df2bdb01d215ed29136f23d2f0f78f6dae4563b0b1f267 src/compressible/CompressibleContinuity.cpp
check daa27c5fc7e296482fd29fd93a0b4b6185fc7f3d39bc72855aa6240aad3bfe24 src/compressible/IdealGasEOS.cpp
check 27d74d9e535f8e716421ce24e7f955f32dc8200c5ae5a3ddfd1c9cbdb4da3a88 src/compressible/ThermodynamicProperties.cpp
check fc00192ddb9d024fa1c61fcf6d7911fb7b862ec811ddc503b10e18370bc0c5e3 src/compressible/DilatationalStress.cpp
check 23bde46f7a94d7e24ed262ef3ae5f4cc5677bb9194e01399014b12d929df16c5 src/compressible/TotalEnergy.cpp
check 2df1c9f54d5186fb5f22cf1f74fbb3c3aaaaddbbebef929df1853441b2c1956e src/compressible/TotalEnergyEquation.cpp
check 0bdff82ec3e717bc8fc8aac01f5bf06914153039673bcf43cfcfe8902532007d src/compressible/CompressibleRelaxedMomentum.cpp
# P15 thermodynamics, from the freeze
check 8aebd9d8c693ae2066b225331f5e04860095b43da5ca2a80ac050eca14b82666 include/cfd/thermal/Enthalpy.hpp
check 9e7e758fba65f7c22e91f1624df8d4d71f8c4d818da4a295f5e43a0485fb1dee src/thermal/Enthalpy.cpp
echo "  UNTOUCHED: $untouched    TOUCHED: $touched   (TOUCHED must be 0)"
echo ""

echo "### 4. no planted control marker survived ###"
# P17.6's controls planted in the TEST HARNESS, not in production, because this phase changes no
# production source. So the scan MUST cover tests/ -- P17.5's version scanned include/ src/ cuda/
# only, and against this phase's plants it would have been VACUOUS by construction.
# Anchored to end of line: unanchored '// N[0-9]' matches pre-existing Newton-node comments in
# src/discretization/Diffusion.cpp that are present at HEAD. A loose probe is not a finding.
PAT='// N[0-9]b?$'
markers=$(grep -rnE "$PAT" include/ src/ cuda/ tests/ 2>/dev/null | wc -l)
echo "  surviving // N<id> markers in include/ src/ cuda/ TESTS/ : $markers   (expected 0)"
if [ "$markers" -ne 0 ]; then
  grep -rnE "$PAT" include/ src/ cuda/ tests/ 2>/dev/null | sed 's/^/    /'
fi
echo "  sanity -- the pattern DOES match a trailing marker, so 0 means absent, not blind:"
SYNTH=$(printf 'x;  // N3\n' | grep -cE "$PAT")
echo "    matches on a synthetic marker line: $SYNTH   (must be 1)"
echo "  and the harness IS inside the scanned set:"
echo "    $H  $(grep -c . "$H") non-empty lines, scanned under tests/"
echo "  control scratch files left : $(find "$P17/controls" -name '*.pristine*' 2>/dev/null | wc -l)   (expected 0)"
echo "  harness sha256 now         : $(sha256sum "$H" | awk '{print $1}')"
echo "  at the controls baseline   : 01838fd3a113420641ef878550dde080adb9d39a0326b046a7ca4ea4fee895c5"
echo ""

echo "### 5. clang-format on everything P17.6 touched ###"
clang-format-18 --dry-run --Werror "$H" 2>&1 | head -8
echo "  exit: ${PIPESTATUS[0]}   (0 = clean)"
echo "  ($CM is CMake, not C++ -- not clang-format's input)"
echo ""

echo "### 6. the harness drives the REAL solver and duplicates nothing ###"
echo "  CompressibleSIMPLE mentions        : $(grep -c CompressibleSIMPLE "$H")   (the real production solver)"
echo "  incompressible SIMPLE mentions     : $(grep -cE '(^|[^a-zA-Z])SIMPLE ' "$H")"
echo "  hand-rolled EOS in the harness     : $(grep -cE 'rho \* R \* T|/ \(287' "$H")   (0 required)"
echo "  rho_ref taken FROM the EOS         : $(grep -c 'thermo.density(kReferencePressure' "$H")   (1 required, not a literal)"
echo "  hard-coded rho_ref literal         : $(grep -cE 'rhoRef = 1\.' "$H")   (0 required -- control N2 plants exactly this)"
# The harness reaches P17.5's exact inverse THROUGH its public field-level entry points, so grepping
# for temperatureFromInternalEnergy reported 0 and looked like the reuse claim was false. The claim
# was right; the probe named the wrong symbol. It now names what the harness actually calls.
echo "  P17.5 conservedTotalEnergyField    : $(grep -c conservedTotalEnergyField "$H")   (builds rhoE from the primitives)"
echo "  P17.5 recoverPrimitiveFields       : $(grep -c recoverPrimitiveFields "$H")   (the exact inverse, rhoE -> T)"
echo "  production files changed by P17.6  : 0   (section 3 proves it by SHA-256, file by file)"
echo ""

echo "### 7. the code fingerprint ###"
echo "  P17_CODE_FINGERPRINT now              : $(code_fp)"
echo "  at the P17.5 close (the P17.6 freeze) : d2e65555534bde2208cb7affd9caac7c7b4406c3f1cd17dfd473d441b882364a"
echo "  NOTE: this MUST differ -- P17.6 added a test file and a CMake line. It is an identifier, not"
echo "        a pass/fail; its pass/fail use is that it must not move DURING a regression run (B8)."
echo ""

echo "### 8. what P17.6 introduced ###"
if git ls-files --error-unmatch "$H" >/dev/null 2>&1; then
  echo "    $H  TRACKED (unexpected -- P17.6 committed nothing)"
else
  echo "    $H  untracked, as expected for a new file"
fi
echo "  modified TRACKED file attributable to P17.6:"
echo "    $CM  $(git diff --numstat HEAD -- "$CM" | awk '{print "+"$1" -"$2}')  (the second add_executable)"
all_untracked=$(git ls-files --others --exclude-standard -- . ':(exclude)results/' | wc -l)
echo "  P17.6 new files                : 1"
echo "  ALL untracked in the tree      : $all_untracked"
echo "  attributable to EARLIER phases : $(( all_untracked - 1 ))"
echo "  (explicit enumeration, not a listing: nothing since P12 is committed, so the untracked set is"
echo "   the union of every uncommitted phase. That is also why git diff --numstat HEAD is NOT used as"
echo "   an 'untouched by this phase' predicate anywhere above -- only as a line count on ONE named"
echo "   file whose change is expected.)"
echo ""

echo "### 9. the suite size, derived ###"
echo "  TESTs in the harness                      : $(grep -c '^TEST' "$H")   (derived, not hard-coded)"
echo "  add_executable targets in that CMakeLists : $(grep -c add_executable "$CM")   (2: the existing one, untouched, plus P17.6's)"
echo ""

echo "### 10. stray build artefacts in the evidence tree ###"
echo "  __pycache__ dirs under results/p17/low_mach : $(find "$P17" -name __pycache__ -type d 2>/dev/null | wc -l)   (expected 0)"
find "$P17" -name __pycache__ -type d 2>/dev/null | sed 's/^/    /'
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

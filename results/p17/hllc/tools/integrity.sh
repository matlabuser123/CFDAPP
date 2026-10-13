#!/usr/bin/env bash
# P17.7 repository integrity (section 41). Run LAST, after every control has restored and the
# regression has finished, so what it reports is the state P17.7 actually leaves behind.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/hllc
HDR=include/cfd/compressible/HllcFlux.hpp
SRC=src/compressible/HllcFlux.cpp
CU=cuda/kernels/DeviceHllcFluxKernel.cu
GHDR=include/cfd/gpu/DeviceHllcFlux.hpp
TST=tests/unit/compressible/test_hllc_flux.cpp
GTST=tests/unit/gpu/test_hllc_gpu_equivalence.cpp
REFH=tests/unit/compressible/hllc_reference_data.hpp
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
echo "P17.7 REPOSITORY INTEGRITY"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### 1. nothing was committed, pushed, reset or stashed ###"
echo "  HEAD                     : $(git rev-parse HEAD)"
echo "  HEAD at the P17.7 freeze : 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274"
if [ "$(git rev-parse HEAD)" = "3bd32f918656f471e72b5f6ac7cb6c2cbf75a274" ]; then
  echo "  HEAD unchanged           : YES"
else
  echo "  HEAD unchanged           : *** NO ***"
fi
echo "  branch                   : $(git rev-parse --abbrev-ref HEAD)"
echo "  stash entries            : $(git stash list | wc -l)   (expected 0)"
echo ""

echo "### 2. the frozen documents are unchanged ###"
(cd "$P17" && sha256sum acceptance_gate.md audit.md tools/exact_riemann.py tools/audit.sh) \
  | sed 's/^/  /'
echo "  expected, from logs/02_freeze.log:"
echo "    1f4ccd8af57f529e1654b2465df9e1c07124c22e0e8a500c56116372009a6465  acceptance_gate.md"
echo "    bd2f77dc8adde786f287797ea98f2a219182dd738f147b27f64484f4fd5f7c4b  audit.md"
echo "    cddf94e5cc227eb6a53906cefecb03670e3e6d9efb2156b378d86af24843d390  tools/exact_riemann.py"
echo "    7cc938786ca73d5d65380302ef231941ac2ba65cd739d473208f5d934d00a399  tools/audit.sh"
echo "  NOTE: exact_riemann.py is the INDEPENDENT reference G6(f) requires. It is frozen, and"
echo "        tools/gen_reference_header.py IMPORTS it rather than editing it -- which is why the"
echo "        hash above still matches after the reference header was generated."
echo ""

echo "### 3. upstream files UNTOUCHED -- SHA-256, never git diff HEAD ###"
echo "  P17.7 is ADDITIVE: it changes no P17.1-P17.6 production source and no core type."
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
# the core types P17.7 must NOT modify (it works on Vector3 components instead)
check 94d5b7c20f530fd9e84ed5f114b58074e26466f410b34b247a883025d20b5896 include/cfd/core/Vector3.hpp
# P17.2 EOS / thermodynamics -- the authoritative pressure and sound speed
check bdd096490437ee602de31695184035e2ce28f0ab4d2da6187e45c709e919324e include/cfd/compressible/IdealGasEOS.hpp
check daa27c5fc7e296482fd29fd93a0b4b6185fc7f3d39bc72855aa6240aad3bfe24 src/compressible/IdealGasEOS.cpp
check 1dd39383f5d87eb77a5b69252737db0cd0640121ea6e19eb5ec3916377008335 include/cfd/compressible/ThermodynamicProperties.hpp
check 27d74d9e535f8e716421ce24e7f955f32dc8200c5ae5a3ddfd1c9cbdb4da3a88 src/compressible/ThermodynamicProperties.cpp
check 16cb9b91ce6fd9de1011b064b3798c5a80304f3e9520722f1a141c9c7a5aa661 include/cfd/compressible/EquationOfState.hpp
# P17.5 total energy -- the authoritative primitive<->conserved conversion
check a15726f9ffce7f14871f18446ad70129df4c9b34de5d3e8fdaf855abbabe265d include/cfd/compressible/TotalEnergy.hpp
check 23bde46f7a94d7e24ed262ef3ae5f4cc5677bb9194e01399014b12d929df16c5 src/compressible/TotalEnergy.cpp
check 02067df44148ef0ed4171780344779cbf25e7743909c0e0142d0e145517f5e03 include/cfd/compressible/TotalEnergyEquation.hpp
check 2df1c9f54d5186fb5f22cf1f74fbb3c3aaaaddbbebef929df1853441b2c1956e src/compressible/TotalEnergyEquation.cpp
# P17.3 / P17.4 and BOTH SIMPLE paths -- section 35
check 2864c6ebb8b85b35ffd32c88544bb427fa3d87cd573569c60b6b53a808d95ca7 src/compressible/CompressibleMassFlux.cpp
check 5396726d935905d450df2bdb01d215ed29136f23d2f0f78f6dae4563b0b1f267 src/compressible/CompressibleContinuity.cpp
check 0bdff82ec3e717bc8fc8aac01f5bf06914153039673bcf43cfcfe8902532007d src/compressible/CompressibleRelaxedMomentum.cpp
check f95f760bbe2abb284ec33161209bf33f25ec9a0fb4a02e170a2232bd17d6d8ed src/compressible/CompressibleSIMPLE.cpp
check fc00192ddb9d024fa1c61fcf6d7911fb7b862ec811ddc503b10e18370bc0c5e3 src/compressible/DilatationalStress.cpp
check 34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1 src/pressure_velocity/SIMPLE.cpp
check 325a7f380be58144794538cd64d0fd4f1324a46b1bc9fa2727d5fa899e2eb9da include/cfd/pressure_velocity/SIMPLE.hpp
# the validation suites section 39 protects
check 593165a1a2fff9ccbad8b3675ccbdff093bcf22c82b5daad90462fe228949acc tests/integration/compressible/test_low_mach_regression.cpp
check 01838fd3a113420641ef878550dde080adb9d39a0326b046a7ca4ea4fee895c5 tests/integration/compressible/test_low_mach_family.cpp
check 9c3ad6502e65543422e95c21e99354380c2cffeedeafd2d8b4b9fa3c83793796 tests/integration/mms/test_mms_compressible.cpp
check c33143bfc886b0695e8a11b852f0035575f8f8389ee24e4d60a47888e863d674 tests/integration/poiseuille/test_poiseuille_validation.cpp
check 8a6061c30da34bff819beca7c563126e636b505dca4c074c85ee6503ccd949a0 tests/integration/cavity/test_cavity_ghia.cpp
echo "  UNTOUCHED: $untouched    TOUCHED: $touched   (TOUCHED must be 0)"
echo ""

echo "### 4. no planted control marker survived ###"
# P17.7's controls planted in include/, so include/ MUST be in the scan. Anchored to end of line:
# unanchored '// V[0-9]' would match ordinary prose.
# THE PROBE HAD TO BE NARROWED, and the reason matters. A tree-wide scan for the marker shape reports
# EIGHT hits -- all of them P17.5's OWN test-case labels in tests/unit/compressible/test_total_energy.cpp,
# whose validation cases are annotated // V1, // V2, // V5. P17.7's controls use the same shape, so a
# tree-wide scan cannot tell a surviving plant from a pre-existing label. That is the same contamination
# P17.5 hit when '// N[0-9]' matched Newton-node comments in Diffusion.cpp.
# The AUTHORITATIVE restore check is the SHA-256 below, not this grep. The grep is scoped to the file the
# controls actually planted in, and the collision is reported rather than hidden.
PAT='// V[0-9]$'
markers=$(grep -cE "$PAT" "$HDR" 2>/dev/null)
echo "  surviving // V<id> markers in the PLANTED file           : $markers   (expected 0)"
if [ "$markers" -ne 0 ]; then
  grep -nE "$PAT" "$HDR" | sed 's/^/    /'
fi
SYNTH=$(printf 'x;  // V3\n' | grep -cE "$PAT")
echo "  sanity -- the pattern matches a synthetic marker line    : $SYNTH   (must be 1, else the 0 is blind)"
collisions=$(grep -rnE "$PAT" include/ src/ cuda/ tests/ 2>/dev/null | grep -vc "HllcFlux.hpp")
echo "  PRE-EXISTING // V<id> labels elsewhere in the tree       : $collisions"
echo "    P17.5's own test annotations, present before P17.7 and NOT plants:"
grep -rnE "$PAT" include/ src/ cuda/ tests/ 2>/dev/null | grep -v "HllcFlux.hpp" | sed 's/^/      /' | head -8
echo "  the planted file IS in the scanned set: $HDR ($(grep -c . "$HDR") non-empty lines)"
echo "  control scratch files left : $(find "$P17/controls" -name '*.pristine*' 2>/dev/null | wc -l)   (expected 0)"
echo "  HllcFlux.hpp sha256 now    : $(sha256sum "$HDR" | awk '{print $1}')"
echo "  at the controls baseline   : db7d90f5f2e64e991d1327ab53899893e7f752c75674cb14e80f2fb60682ee01"
echo "  expected NOW               : 98885f49b987d1ec09b93492aa12231277296b1cb6f34bb4a1e92cf08b5c0cdd"
echo "    The hash MOVED after the controls, legitimately: criterion E5 (clang-format) FAILED on this"
echo "    file, so clang-format -i was run afterwards. Formatting only -- the token multiset is"
echo "    IDENTICAL before and after, and logs/08_postformat.log shows 39/39 passing with every measured"
echo "    value byte-identical to logs/03_hllc_measured.log. logs/HllcFlux.hpp.preformat is kept so the"
echo "    claim is checkable."
echo ""

echo "### 5. clang-format on everything P17.7 added ###"
clang-format-18 --dry-run --Werror "$HDR" "$SRC" "$GHDR" "$TST" "$GTST" 2>&1 | head -10
echo "  exit: ${PIPESTATUS[0]}   (0 = clean)"
echo "  NOTE: $CU is CUDA. clang-format is not run on .cu files anywhere in this project."
echo "        $REFH is GENERATED and is not hand-formatted either."
echo ""

echo "### 6. no duplicate EOS, sound speed, or primitive/conserved conversion (sections 2, 3, 41) ###"
echo "  --- inside the flux layer, all of these MUST be 0 ---"
printf '    %-46s %s\n' "p = rho R T style pressure formula" \
  "$(grep -cE 'density \* .*gasConstant \* |rho \* R \* T' "$HDR" "$SRC" | awk -F: '{s+=$2} END{print s+0}')"
printf '    %-46s %s\n' "sqrt(gamma R T) sound-speed formula" \
  "$(grep -cE 'sqrt.*gamma.*gasConstant|sqrtR\(.*gamma \* ' "$HDR" "$SRC" | awk -F: '{s+=$2} END{print s+0}')"
printf '    %-46s %s\n' "(gamma-1) energy/pressure inversion" \
  "$(grep -cE '\(gamma - 1' "$HDR" "$SRC" | awk -F: '{s+=$2} END{print s+0}')"
echo "  --- and these MUST be the authoritative calls ---"
printf '    %-46s %s\n' "speedOfSound() call sites in the factory" "$(grep -c 'speedOfSound(' "$SRC")"
printf '    %-46s %s\n' "equationOfState().pressure() call sites" "$(grep -c 'equationOfState().pressure' "$SRC")"
printf '    %-46s %s\n' "conservedTotalEnergy() call sites" "$(grep -c 'conservedTotalEnergy(' "$SRC")"
printf '    %-46s %s\n' "recover() call sites (P17.5's inverse)" "$(grep -c '\.recover(' "$SRC")"
echo "  --- the .cu contains NO physics (section 28 / criterion G1) ---"
printf '    %-46s %s\n' "wave-speed / star-state formulas in the .cu" \
  "$(grep -cE 'sStar|rhoStar|q_L|shockFactor|pPvrs' "$CU")"
printf '    %-46s %s\n' "calls INTO the shared header" \
  "$(grep -cE 'cfd::compressible::(hllcFlux|hllFluxForControlOnly)' "$CU")"
echo ""

echo "### 7. section 35 -- no SIMPLE path routes through HLLC ###"
printf '  %-52s %s\n' "HLLC references from any SIMPLE source" \
  "$(grep -rliI 'Hllc' src/pressure_velocity/ src/compressible/CompressibleSIMPLE.cpp 2>/dev/null | wc -l)"
printf '  %-52s %s\n' "HLLC refs in src/ CODE outside HllcFlux.cpp (CMake excluded)" \
  "$(grep -rliI 'Hllc' src/ 2>/dev/null | grep -v 'HllcFlux.cpp' | grep -v 'CMakeLists.txt' | wc -l)"
echo "  (0 and 0 required: the two solver architectures coexist and are not wired together)"
echo ""

echo "### 8. the code fingerprint ###"
echo "  P17_CODE_FINGERPRINT now              : $(code_fp)"
echo "  at the P17.6 close (the P17.7 freeze) : cf4d53a2e6b090228f68d58bc9b452d324abd3dde03539a53a70f44542c70bdf"
echo "  NOTE: this MUST differ -- P17.7 added code. It is an identifier, not a pass/fail; its"
echo "        pass/fail use is that it must not move DURING a regression run (B8)."
echo ""

echo "### 9. what P17.7 introduced ###"
for f in "$HDR" "$SRC" "$GHDR" "$CU" "$TST" "$GTST" "$REFH"; do
  if git ls-files --error-unmatch "$f" >/dev/null 2>&1; then
    printf '    %-56s TRACKED (unexpected -- P17.7 committed nothing)\n' "$f"
  else
    printf '    %-56s untracked, as expected for a new file\n' "$f"
  fi
done
echo "  modified TRACKED files P17.7 edited:"
echo "    CAUTION: git diff HEAD on these shows the union of EVERY uncommitted phase, because nothing"
echo "    since P12 is committed. A first version of this probe printed those totals as 'attributable"
echo "    to P17.7', which was WRONG -- src/CMakeLists.txt reads +42 while P17.7 added 3 lines to it."
echo "    The HLLC-attributable count is therefore measured BY CONTENT, not by diff size. This is the"
echo "    same trap that makes git diff --numstat HEAD unusable as an 'untouched by this phase' test."
printf '    %-42s %-17s %s\n' "file" "all uncommitted" "of which P17.7"
for f in src/CMakeLists.txt cuda/CMakeLists.txt tests/unit/compressible/CMakeLists.txt \
         tests/unit/gpu/CMakeLists.txt; do
  allp=$(git diff HEAD -- "$f" | grep -c '^+')
  mine=$(git diff HEAD -- "$f" | grep '^+' | grep -ciE 'hllc|fmad')
  printf '    %-42s %-17s %s\n' "$f" "+$allp" "$mine"
done
all_untracked=$(git ls-files --others --exclude-standard -- . ':(exclude)results/' | wc -l)
echo "  P17.7 new files                : 7"
echo "  ALL untracked in the tree      : $all_untracked"
echo "  attributable to EARLIER phases : $(( all_untracked - 7 ))"
echo "  (explicit enumeration, not a listing: nothing since P12 is committed, so the untracked set is"
echo "   the union of every uncommitted phase. That is also why git diff --numstat HEAD is NOT used as"
echo "   an 'untouched by this phase' predicate above -- only as a line count on NAMED files whose"
echo "   change is expected.)"
echo ""

echo "### 10. the suite size, derived ###"
echo "  TESTs in test_hllc_flux.cpp            : $(grep -c '^TEST(' "$TST")   (derived)"
echo "  TESTs in test_hllc_gpu_equivalence.cpp : $(grep -c '^TEST(' "$GTST")   (derived)"
echo "  total P17.7 TESTs                      : $(( $(grep -c '^TEST(' "$TST") + $(grep -c '^TEST(' "$GTST") ))"
echo ""

echo "### 11. stray artefacts in the evidence tree ###"
echo "  __pycache__ dirs under results/p17/hllc : $(find "$P17" -name __pycache__ -type d 2>/dev/null | wc -l)   (expected 0)"
find "$P17" -name __pycache__ -type d 2>/dev/null | sed 's/^/    /'
echo "  leftover /tmp probe binaries            : $(ls /tmp/p17_7_fp 2>/dev/null | wc -l)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

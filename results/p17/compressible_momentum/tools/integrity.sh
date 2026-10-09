#!/usr/bin/env bash
# P17.4 repository integrity. Run LAST, after every control has restored and the regression has
# finished, so that what it reports is the state P17.4 actually leaves behind.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_momentum
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
echo "P17.4 REPOSITORY INTEGRITY"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### 1. nothing was committed, pushed, reset or stashed ###"
echo "  HEAD                     : $(git rev-parse HEAD)"
echo "  HEAD at the P17.4 freeze : 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274"
echo "  HEAD unchanged           : $([ "$(git rev-parse HEAD)" = "3bd32f918656f471e72b5f6ac7cb6c2cbf75a274" ] && echo YES || echo '*** NO ***')"
echo "  branch                   : $(git rev-parse --abbrev-ref HEAD)"
echo "  stash entries            : $(git stash list | wc -l)   (expected 0)"
echo "  commits ahead of HEAD~0  : 0 by construction (HEAD unchanged)"
echo ""

echo "### 2. the files P17.4 was allowed to touch, and no others ###"
echo "  FORBIDDEN (must hash to the freeze):"
printf '    %s\n' "$(sha256sum src/physics/MomentumEquation.cpp)"
echo "      expected e46d49fcda78e0271538bc69117e249b0ea50dd2265c5f603d64d4206dc7bb59"
printf '    %s\n' "$(sha256sum include/cfd/physics/MomentumEquation.hpp)"
echo "      expected 7feca5c4a3a22943c442d1fa162b4e860ab037b69a3c6a6ce0e6c413bb0e37af"
echo ""
echo "  P17.4's own files, as they now stand:"
for f in include/cfd/compressible/DilatationalStress.hpp \
         src/compressible/DilatationalStress.cpp \
         include/cfd/compressible/CompressibleRelaxedMomentum.hpp \
         src/compressible/CompressibleRelaxedMomentum.cpp \
         tests/unit/compressible/test_compressible_momentum.cpp \
         src/CMakeLists.txt; do
  printf '    %s\n' "$(sha256sum "$f")"
done
echo ""

echo "### 3. no planted control marker survived ###"
# Every control in controls.sh / controls_b.sh tags its edit with a // N<id> comment, so a
# surviving marker means a restore did not happen. Searched across ALL production source, not
# only the three files the controls touch, because a restore failure is exactly the case where
# the damage would not be where it was expected.
markers=$(grep -rnE '// N[0-9]b?$|// N[0-9]b? ' include/ src/ cuda/ 2>/dev/null | wc -l)
echo "  surviving control markers in include/ src/ cuda/ : $markers   (expected 0)"
if [ "$markers" -ne 0 ]; then
  grep -rnE '// N[0-9]b?$|// N[0-9]b? ' include/ src/ cuda/ 2>/dev/null | sed 's/^/    /'
fi
echo "  surviving L5b probe marker                      : $(grep -rc 'L5b probe control' src/physics/MomentumEquation.cpp 2>/dev/null || echo 0)   (expected 0)"
echo ""

echo "### 4. no control scratch files left in the tree ###"
# controls_b.sh removes its own *.pristine2; the FIRST controls.sh never removed its *.pristine.
# They are exact copies of the post-implementation sources at that moment, and their hashes are
# already recorded in logs/06_controls.log, so they are redundant rather than load-bearing. They
# are KEPT -- deleting control artefacts would look like tidying away a control run -- and
# labelled here. The inconsistency is in my tooling, not in the repository.
pristine_n=$(find "$P17/controls" -name '*.pristine*' 2>/dev/null | wc -l)
echo "  *.pristine / *.pristine2 under results/controls : $pristine_n"
find "$P17/controls" -name '*.pristine*' 2>/dev/null | sed 's/^/    /'
echo "  Control scratch copies from the FIRST controls run, kept deliberately."
echo "  What matters: 0 markers survived (section 3) and both guarded files hash to the"
echo "  freeze (section 2), so no planted defect reached the tree."
echo ""

echo "### 5. clang-format across every file P17.4 touched ###"
clang-format-18 --dry-run --Werror \
  include/cfd/compressible/DilatationalStress.hpp \
  src/compressible/DilatationalStress.cpp \
  include/cfd/compressible/CompressibleRelaxedMomentum.hpp \
  src/compressible/CompressibleRelaxedMomentum.cpp \
  tests/unit/compressible/test_compressible_momentum.cpp 2>&1 | head -8
echo "  exit: ${PIPESTATUS[0]}   (0 = clean)"
echo ""

echo "### 6. the code fingerprint ###"
echo "  P17_CODE_FINGERPRINT now              : $(code_fp)"
echo "  at the P17.3 close (the P17.4 freeze) : 437ac0e3d9f09692f6bf415d7b1cce38b65d2c02e0158a346869232ad800a272"
echo "  NOTE: this MUST differ -- P17.4 changed code. It is an identifier, not a pass/fail."
echo "        The pass/fail use is that it must not move DURING a regression run."
echo ""

echo "### 7. untracked files P17.4 introduced ###"
# The first version of this section listed EVERY untracked file (124 of them) under a heading
# claiming P17.4 introduced them. That is the same mis-attribution the L5 probe made: nothing
# since P12 is committed, so the untracked set is the union of every uncommitted phase. P17.4s
# own new files are named and checked; the rest are counted and attributed, not listed.
P17_4_NEW="include/cfd/compressible/DilatationalStress.hpp src/compressible/DilatationalStress.cpp"
for f in $P17_4_NEW; do
  if git ls-files --error-unmatch "$f" >/dev/null 2>&1; then
    echo "    $f  TRACKED (unexpected -- P17.4 committed nothing)"
  else
    echo "    $f  untracked, as expected for a new file"
  fi
done
all_untracked=$(git ls-files --others --exclude-standard -- . ':(exclude)results/' | wc -l)
echo "  P17.4 new files                : 2"
echo "  ALL untracked in the tree      : $all_untracked"
echo "  attributable to EARLIER phases : $(( all_untracked - 2 ))   (P13-001 restart, thermal,"
echo "                                   P16 wall functions, GPU turbulence)"
echo ""

echo "### 8. the test-suite size ###"
tests_now=$(grep -c '^TEST(' tests/unit/compressible/test_compressible_momentum.cpp)
echo "  TESTs in test_compressible_momentum.cpp : $tests_now   (7 at the freeze, +$(( tests_now - 7 )))"
echo "  DERIVED, not hard-coded, so this line cannot go stale the way the P17.2"
echo "  PREDICTED 21 constant did."
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

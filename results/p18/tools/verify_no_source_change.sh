#!/usr/bin/env bash
# P18.0 sections 33-35 -- prove that P18.0 changed no code, and clean up the regression's noise.
#
# WHY A SCRIPT FILE: the fingerprint needs a git pathspec ARRAY with :(exclude) entries. Passed
# through `wsl.exe -- bash -c "..."` those get mangled into plausible-but-wrong values -- the
# untracked-set hash came back EMPTY on the first attempt. A file is the only reliable form here.
#
# WHY THE RAW `git status` COUNT IS NOT THE RIGHT TEST: it lists `?? include/cfd/transport/` as one
# entry because git collapses untracked DIRECTORIES. Those are P14 files never committed, present
# long before P18.0. Counting status entries therefore cannot distinguish "P18.0 created this" from
# "this was already untracked". The FINGERPRINT COMPONENTS can: they hash every untracked code file's
# contents, so if either component is unchanged, no code file was added, removed or edited.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1

DOCEXC=(. ':(exclude)results/' ':(exclude)TODO.md' ':(exclude)ROADMAP.md' ':(exclude)CLAUDE.md'
        ':(exclude)README.md' ':(exclude)CONTRIBUTING.md' ':(exclude)QUALITY_GATE.md'
        ':(exclude)PROJECT_STRUCTURE.md' ':(exclude)RELEASE_NOTES_*.md' ':(exclude)docs/')

EXPECT_T=b4500002367b115c3b321114b598ffa98bd1965d089f9aa1c7fc8d4527e8831e
EXPECT_U=ff02adfe311c6832573842cda6d35175f724046d0395cbb1aa5b1c0988f8cdcf
EXPECT_FP=60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318

T=$(git diff --binary HEAD -- "${DOCEXC[@]}" | sha256sum | awk '{print $1}')
U=$(git ls-files --others --exclude-standard -- "${DOCEXC[@]}" \
    | LC_ALL=C sort | while read -r f; do sha256sum "$f"; done | sha256sum | awk '{print $1}')
FP=$(printf '%s %s' "$T" "$U" | sha256sum | awk '{print $1}')

fail=0
echo "=== did P18.0 change any CODE? ==="
printf '  tracked-diff   now %s\n                 exp %s  %s\n' "$T" "$EXPECT_T" \
  "$([ "$T" = "$EXPECT_T" ] && echo MATCH || { echo CHANGED; fail=1; })"
printf '  untracked-set  now %s\n                 exp %s  %s\n' "$U" "$EXPECT_U" \
  "$([ "$U" = "$EXPECT_U" ] && echo MATCH || { echo CHANGED; fail=1; })"
printf '  FINGERPRINT    now %s\n                 exp %s  %s\n' "$FP" "$EXPECT_FP" \
  "$([ "$FP" = "$EXPECT_FP" ] && echo MATCH || { echo CHANGED; fail=1; })"
echo ""
if [ "$fail" -eq 0 ]; then
  echo "  VERDICT: no code file was added, removed or modified by P18.0."
  echo "  The untracked-directory entries under include/cfd/transport and src/transport are P14"
  echo "  files that were"
  echo "  already untracked at P18.0 start -- their CONTENTS are inside the unchanged untracked-set"
  echo "  hash, which is what makes this a proof rather than an assertion."
else
  echo "  *** CODE CHANGED -- sections 33/34 violated, investigate before proceeding ***"
fi

echo ""
echo "=== regression runtime noise under results/validation ==="
N=$(git status --porcelain -- results/validation | wc -l)
echo "  files rewritten by the baseline regression: $N"
if [ "$N" -gt 0 ]; then
  echo "  --- classifying every changed line (fails closed on anything non-timing) ---"
  python3 results/p17/cpu_gpu_equivalence/tools/md_column_audit.py
  rc=$?
  echo "  classifier exit: $rc"
  if [ "$rc" -ne 0 ]; then
    echo "  *** NON-TIMING CHANGES -- do NOT restore blindly; investigate ***"
    exit 1
  fi
  if pgrep -f p18_baseline_regress > /dev/null 2>&1; then
    echo "  A BASELINE CONFIGURATION IS STILL RUNNING -- not restoring, because git checkout would"
    echo "  discard output the in-flight run is still writing. Re-run this script after it finishes."
    exit 2
  fi
  git checkout -- results/validation
  echo "  restored. remaining modified: $(git status --porcelain -- results/validation | wc -l)"
fi
exit "$fail"

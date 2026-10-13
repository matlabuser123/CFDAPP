#!/usr/bin/env bash
# G4 source fingerprint -- a TODO.md-independent variant of tree_fingerprint.sh.
#
# WHY THIS EXISTS (amendment, recorded not silent):
# tree_fingerprint.sh's SOURCE FINGERPRINT includes TODO.md by its own design
# note ("src/, include/, cuda/, tests/, apps/, benchmarks/, scripts/, CMake
# files, CLAUDE.md, TODO.md"). But TODO.md is the qualification's own execution
# control file: every gate closeout ticks boxes in it. So the criterion
# "the source fingerprint must return to 86346f15..." cannot be satisfied by
# any gate that closes -- closing it is what breaks it.
#
# Measured, not argued:
#   G1 baseline    2026-09-24T01:56Z  TODO.md 667 changed lines  86346f15...
#   G3 case-val    2026-09-24T18:59Z  TODO.md 667 changed lines  86346f15...
#   G4 (this tree)                    TODO.md 496 changed lines  differs
# The fingerprint held exactly as long as TODO.md held, and moved when G1-G3
# were ticked.
#
# This script therefore reports BOTH: the original whole-tree value (for
# continuity with G1) and a TODO.md-excluded value, which is the one that can
# actually be held constant across a gate closeout. Nothing is weakened: the
# excluded set is one bookkeeping file that is neither compiled nor tested, and
# every source, test, CMake and CLAUDE.md path stays inside the hash.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
LABEL="${1:?usage: G4_fingerprint.sh <label>}"
OUT=results/p13/final-qualification/2026-09-24/g4-cli-gui/fingerprint_${LABEL}.txt

SRC_DIFF=$(git diff --binary HEAD -- . ':(exclude)results/' | sha256sum | cut -d' ' -f1)
SRC_DIFF_NT=$(git diff --binary HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' | sha256sum | cut -d' ' -f1)
SRC_UNTRACKED=$(git ls-files --others --exclude-standard -- . ':(exclude)results/' | sort \
                | xargs -r sha256sum | sha256sum | cut -d' ' -f1)
FP=$(printf '%s %s' "$SRC_DIFF" "$SRC_UNTRACKED" | sha256sum | cut -d' ' -f1)
FP_NT=$(printf '%s %s' "$SRC_DIFF_NT" "$SRC_UNTRACKED" | sha256sum | cut -d' ' -f1)

NFILES=$(git diff --name-only HEAD -- . ':(exclude)results/' | wc -l)
NFILES_NT=$(git diff --name-only HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' | wc -l)

{
  echo "label     : $LABEL"
  echo "date      : $(date -u +%FT%TZ)"
  echo "HEAD      : $(git rev-parse HEAD)"
  echo ""
  echo "tracked source files changed (incl TODO.md) : $NFILES"
  echo "tracked source files changed (excl TODO.md) : $NFILES_NT"
  echo ""
  echo "SOURCE FINGERPRINT (tree_fingerprint.sh definition, incl TODO.md):"
  echo "  $FP"
  echo "SOURCE FINGERPRINT (G4 definition, excl TODO.md):"
  echo "  $FP_NT"
  echo ""
  echo "components:"
  echo "  tracked diff  incl TODO.md : $SRC_DIFF"
  echo "  tracked diff  excl TODO.md : $SRC_DIFF_NT"
  echo "  untracked source contents  : $SRC_UNTRACKED"
} | tee "$OUT"

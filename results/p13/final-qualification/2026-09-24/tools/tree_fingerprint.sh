#!/usr/bin/env bash
# P13 Final Qualification -- WORKING-TREE FINGERPRINT.
#
# WHY THIS EXISTS
# ---------------
# `3bd32f918656-modified` is not a reproducible identity. It named the tree on
# 2026-09-22 when "modified" meant "P13 is uncommitted", and it names the tree
# today when "modified" additionally means "plus all of P14, P15 and P16". Two
# materially different trees, one string. Nothing can be qualified against an
# identity that cannot tell them apart.
#
# So the qualification records a fingerprint of the ACTUAL working tree, even
# though it is deliberately not committed. Run at G1 and again at G10: if the
# hash moved, the final report must say which authorized fix moved it and which
# gates were re-run afterwards. That is what prevents qualifying one tree and
# reporting another.
#
# Deliberately a script, not a command typed twice: the two fingerprints are
# only comparable if they are computed the same way.
#
# Usage: tree_fingerprint.sh <label>
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
LABEL="${1:?usage: tree_fingerprint.sh <label>}"
OUT=results/p13/final-qualification/2026-09-24/fingerprint_${LABEL}
mkdir -p "$(dirname "$OUT")"

{
  echo "label     : $LABEL"
  echo "date      : $(date -u +%FT%TZ)"
  echo "HEAD      : $(git rev-parse HEAD)"
  echo "HEAD short: $(git rev-parse --short=12 HEAD)"
  echo "branch    : $(git rev-parse --abbrev-ref HEAD 2>/dev/null)"
  echo "dirty     : $([ -n "$(git status --porcelain)" ] && echo YES || echo NO)"
  echo ""
  echo "=== tracked changes, stat ==="
  git diff --stat HEAD
  echo ""
  echo "=== status (short) ==="
  git status --short
  echo ""
  echo "=== untracked files (excluding ignored) ==="
  git ls-files --others --exclude-standard | sort
} > "${OUT}.txt" 2>&1

# TWO fingerprints, and the distinction is the whole point.
#
# The FULL one covers everything. It WILL change during qualification, because
# qualification writes evidence under results/ -- 651 untracked files at G1,
# most of them evidence. A fingerprint that always changes proves nothing at
# G10, which is why the full one is recorded but is NOT the comparison.
#
# The SOURCE one excludes results/ and is the one that must NOT move. It covers
# what is actually built and tested: src/, include/, cuda/, tests/, apps/,
# benchmarks/, scripts/, CMake files, CLAUDE.md, TODO.md. If THAT moves between
# G1 and G10, a production or test file changed mid-qualification, and the final
# report must name the authorized fix and the gates re-run after it.
# EXCLUSIONS -- widened twice, each time after the instrument produced a false
# reading rather than after theorising about one.
#
#   1. results/          qualification writes its own evidence there.
#   2. */results/        TEST FIXTURES write output into tracked directories:
#                        cases/heated_cavity/results/metadata.json and three
#                        under tests/data/cases/*/results/. A pathspec of
#                        ':(exclude)results/' matches only the TOP-LEVEL dir, so
#                        simply RUNNING the gates moved the 'source' hash.
#   3. TODO.md           a control document the user edits during the campaign.
#                        A doc edit must not read as a source-integrity failure.
#
# What remains is what the claim is about: production code, headers, CUDA, apps,
# tests, scripts, benchmarks and the build system.
EXCLUDE_EVIDENCE=':(exclude)results/'
EXCLUDE_TESTOUT=':(exclude)*/results/'
EXCLUDE_TODO=':(exclude)TODO.md'

DIFF_HASH=$(git diff --binary HEAD | sha256sum | cut -d' ' -f1)
SRC_DIFF_HASH=$(git diff --binary HEAD -- . "$EXCLUDE_EVIDENCE" "$EXCLUDE_TESTOUT" "$EXCLUDE_TODO" | sha256sum | cut -d' ' -f1)

UNTRACKED_LIST=$(git ls-files --others --exclude-standard | sort)
SRC_UNTRACKED_LIST=$(git ls-files --others --exclude-standard -- . "$EXCLUDE_EVIDENCE" "$EXCLUDE_TESTOUT" "$EXCLUDE_TODO" | sort)

hash_list() {
  if [ -z "$1" ]; then echo "none"; else
    printf '%s\n' "$1" | xargs -r sha256sum 2>/dev/null | sha256sum | cut -d' ' -f1
  fi
}
UNTRACKED_HASH=$(hash_list "$UNTRACKED_LIST")
SRC_UNTRACKED_HASH=$(hash_list "$SRC_UNTRACKED_LIST")

COMBINED=$(printf '%s %s' "$DIFF_HASH" "$UNTRACKED_HASH" | sha256sum | cut -d' ' -f1)
SOURCE_FP=$(printf '%s %s' "$SRC_DIFF_HASH" "$SRC_UNTRACKED_HASH" | sha256sum | cut -d' ' -f1)
SRC_CHANGED=$(git diff --name-only HEAD -- . "$EXCLUDE_EVIDENCE" "$EXCLUDE_TESTOUT" "$EXCLUDE_TODO" | wc -l)

{
  echo ""
  echo "=== FINGERPRINT ==="
  echo "full: tracked diff sha256      : $DIFF_HASH"
  echo "full: untracked files sha256   : $UNTRACKED_HASH"
  echo "FULL COMBINED                  : $COMBINED"
  echo "   (EXPECTED to change -- qualification writes evidence under results/)"
  echo ""
  echo "source: tracked diff sha256    : $SRC_DIFF_HASH"
  echo "source: untracked files sha256 : $SRC_UNTRACKED_HASH"
  echo "SOURCE FINGERPRINT             : $SOURCE_FP"
  echo "   (must NOT change between G1 and G10 without a named authorized fix)"
} >> "${OUT}.txt"

echo "label=$LABEL"
echo "HEAD=$(git rev-parse --short=12 HEAD)"
echo "FULL COMBINED      = $COMBINED   (will change: evidence is written)"
echo "SOURCE FINGERPRINT = $SOURCE_FP   (must not change)"
echo "source files changed vs HEAD = $SRC_CHANGED"
echo "written: ${OUT}.txt"

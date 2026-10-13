#!/usr/bin/env bash
# P17.0 -- attribute the V2 move, and compute a documentation-invariant CODE fingerprint.
# Read-only on the repo except results/p17/baseline/.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT" || exit 1
OUT=$ROOT/results/p17/baseline

# ---- (a) the P17.2-recorded source hash must still hold ----
echo "=== (a) P17.2 recorded source hash, re-verified ==="
ACT=$(sha256sum src/compressible/IdealGasEOS.cpp | awk '{print $1}')
echo "  recorded by P17.2 controls : aba335fe053620482e8fdb0b294226ca5742bb97bf7dae3bde8cc436a027f8d0"
echo "  measured now               : $ACT"
[ "$ACT" = "aba335fe053620482e8fdb0b294226ca5742bb97bf7dae3bde8cc436a027f8d0" ] \
  && echo "  VERDICT: MATCH -- the P17.2 source is untouched" \
  || echo "  VERDICT: *** MISMATCH ***"
echo

# ---- (b) which tracked changed files are documentation ----
echo "=== (b) documentation files inside the V2 tracked component ==="
git diff --name-only HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' \
  | grep -E '\.md$|^docs/' | LC_ALL=C sort | sed 's/^/  /'
echo "  count: $(git diff --name-only HEAD -- . ':(exclude)results/' ':(exclude)TODO.md' | grep -cE '\.md$|^docs/')"
echo

# ---- (c) CODE fingerprint: V2's method, additionally excluding documentation ----
# NOT comparable with V2 numerically. Different file set, different name.
EXC=(. ':(exclude)results/' ':(exclude)TODO.md' ':(exclude)ROADMAP.md' ':(exclude)CLAUDE.md'
     ':(exclude)README.md' ':(exclude)CONTRIBUTING.md' ':(exclude)QUALITY_GATE.md'
     ':(exclude)PROJECT_STRUCTURE.md' ':(exclude)RELEASE_NOTES_*.md' ':(exclude)docs/')
T=$(git diff --binary HEAD -- "${EXC[@]}" | sha256sum | awk '{print $1}')
U=$(git ls-files --others --exclude-standard -- "${EXC[@]}" \
    | LC_ALL=C sort | while read -r f; do sha256sum "$f"; done | sha256sum | awk '{print $1}')
CODE=$(printf '%s %s' "$T" "$U" | sha256sum | awk '{print $1}')
NT=$(git diff --name-only HEAD -- "${EXC[@]}" | wc -l)
NU=$(git ls-files --others --exclude-standard -- "${EXC[@]}" | wc -l)

{
echo "P17_CODE_FINGERPRINT -- documentation-invariant companion to SOURCE_FINGERPRINT_V2"
echo "date   : $(date -u +%FT%TZ)"
echo "HEAD   : $(git rev-parse HEAD)"
echo ""
echo "WHY THIS EXISTS"
echo "  SOURCE_FINGERPRINT_V2 excludes exactly results/** and TODO.md. Every other"
echo "  markdown file -- ROADMAP.md, CLAUDE.md, README.md, docs/** -- is INSIDE it."
echo "  On 2026-09-26T03:58:06Z ROADMAP.md was rewritten (624+/257-) by work outside"
echo "  P17. V2 moved fb69dc2a -> fbf78665 with ZERO source change. V2 is therefore a"
echo "  CONTAMINATED instrument for the question P17 needs to ask repeatedly:"
echo "  'did any code change?'"
echo ""
echo "  V2 IS NOT REDEFINED. It is preserved verbatim and still recorded, because it is"
echo "  the only value comparable with P13's G4..G10 chain. This is an ADDITIONAL,"
echo "  NARROWER instrument with its own name. A CODE value must NEVER be compared"
echo "  numerically with a V2 value: they hash different file sets."
echo ""
echo "EXACT FILE SELECTION -- V2's algorithm, with documentation additionally excluded"
echo "  EXCLUDE = results/**  TODO.md  ROADMAP.md  CLAUDE.md  README.md  CONTRIBUTING.md"
echo "            QUALITY_GATE.md  PROJECT_STRUCTURE.md  RELEASE_NOTES_*.md  docs/**"
echo "  1. tracked   : sha256 of git diff --binary HEAD over that set"
echo "  2. untracked : sha256 of the sorted per-file sha256 list over that set"
echo "  3. CODE      = sha256(\"<tracked> <untracked>\")"
echo ""
echo "file counts"
echo "  tracked code files changed : $NT   (V2 counts 116)"
echo "  untracked code+output files: $NU   (V2 counts 122)"
echo ""
echo "components"
echo "  tracked diff sha256      : $T"
echo "  untracked contents sha256: $U"
echo ""
echo "P17_CODE_FINGERPRINT = $CODE"
} > "$OUT/code_fingerprint.txt"

echo "=== (c) CODE fingerprint ==="
echo "  tracked code files : $NT   (V2: 116)"
echo "  P17_CODE_FINGERPRINT = $CODE"

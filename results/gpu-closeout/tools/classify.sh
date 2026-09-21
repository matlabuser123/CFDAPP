#!/usr/bin/env bash
# GPU closeout Phase 1 -- classify every change in the working tree.
#
#   A  intended production implementation
#   B  intended tests
#   C  intended documentation / TODO
#   D  intended evidence / results
#   E  generated or timing-only churn        -- MUST NOT be committed
#   F  accidental / unexplained              -- MUST NOT be committed
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT"

classify() {
  case "$1" in
    results/validation/*)                       echo E ;;
    results/*)                                  echo D ;;
    cases/*/results/*|tests/data/cases/*/results/*) echo E ;;
    src/*|include/*|cuda/*|apps/*)              echo A ;;
    tests/*)                                    echo B ;;
    TODO.md|ROADMAP.md|CLAUDE.md|README.md|CONTRIBUTING.md|docs/*|*.md) echo C ;;
    CMakeLists.txt|CMakePresets.json|cmake/*)   echo A ;;
    .gitignore|.clang-format|.clang-tidy|.github/*) echo C ;;
    benchmarks/*|tools/*|python/*|schemas/*|scripts/*|examples/*|validation/*) echo C ;;
    *)                                          echo F ;;
  esac
}

echo "=== TRACKED MODIFICATIONS ==="
git diff --name-only | while read -r f; do printf "%s  %s\n" "$(classify "$f")" "$f"; done \
  | sort > /tmp/cls_tracked.txt
awk '{print $1}' /tmp/cls_tracked.txt | sort | uniq -c | sort -rn
echo ""
echo "--- A / B / C (listed in full) ---"
grep -E "^[ABC]  " /tmp/cls_tracked.txt
echo ""
echo "--- E (generated / timing churn) : $(grep -c '^E  ' /tmp/cls_tracked.txt) files ---"
echo "--- F (unexplained)              : $(grep -c '^F  ' /tmp/cls_tracked.txt) files ---"
grep -E "^F  " /tmp/cls_tracked.txt || echo "    none"

echo ""
echo "=== UNTRACKED FILES ==="
git ls-files --others --exclude-standard | while read -r f; do
  printf "%s  %s\n" "$(classify "$f")" "$f"; done | sort > /tmp/cls_untracked.txt
awk '{print $1}' /tmp/cls_untracked.txt | sort | uniq -c | sort -rn
echo ""
echo "--- A / B (production + tests, listed in full) ---"
grep -E "^[AB]  " /tmp/cls_untracked.txt
echo ""
echo "--- F (unexplained) ---"
grep -E "^F  " /tmp/cls_untracked.txt || echo "    none"
echo ""
echo "--- E among untracked ---"
grep -E "^E  " /tmp/cls_untracked.txt | head -10 || echo "    none"

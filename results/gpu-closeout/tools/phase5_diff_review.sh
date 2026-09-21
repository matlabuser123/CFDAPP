#!/usr/bin/env bash
# GPU closeout Phase 5 -- review the complete final diff before committing.
#
# Runs AFTER `git add -A`, against the INDEX, so what is reviewed is exactly
# what will be committed -- not the working tree, which also contains ignored
# files. A review of something other than the thing being committed is not a
# review.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-closeout
cd "$ROOT"

echo "=== what is STAGED, by category ==="
git diff --cached --name-status > /tmp/staged.txt
awk '{print $1}' /tmp/staged.txt | sort | uniq -c | sed 's/^/  /'
echo "  total staged: $(wc -l < /tmp/staged.txt)"

classify() {
  case "$1" in
    results/validation/*)                       echo E ;;
    results/*)                                  echo D ;;
    cases/*/results/*|tests/data/cases/*/results/*) echo E ;;
    src/*|include/*|cuda/*|apps/*)              echo A ;;
    tests/*)                                    echo B ;;
    CMakeLists.txt|CMakePresets.json|cmake/*)   echo A ;;
    .gitignore|.clang-format|.clang-tidy|.github/*) echo C ;;
    *.md|docs/*)                                echo C ;;
    *)                                          echo F ;;
  esac
}

echo ""
echo "=== category counts ==="
awk '{print $2}' /tmp/staged.txt | while read -r f; do classify "$f"; done \
  | sort | uniq -c | sed 's/^/  /'

echo ""
echo "=== production files (A) ==="
awk '{print $2}' /tmp/staged.txt | while read -r f; do
  [ "$(classify "$f")" = A ] && echo "  $f"; done
echo ""
echo "=== test files (B) ==="
awk '{print $2}' /tmp/staged.txt | while read -r f; do
  [ "$(classify "$f")" = B ] && echo "  $f"; done
echo ""
echo "=== documentation (C) ==="
awk '{print $2}' /tmp/staged.txt | while read -r f; do
  [ "$(classify "$f")" = C ] && echo "  $f"; done
echo ""
echo "=== evidence (D): $(awk '{print $2}' /tmp/staged.txt | while read -r f; do classify "$f"; done | grep -c D) files ==="
echo ""
echo "=== TIMING CHURN (E) -- must be ZERO ==="
n=$(awk '{print $2}' /tmp/staged.txt | while read -r f; do
      [ "$(classify "$f")" = E ] && echo "$f"; done | wc -l)
if [ "$n" -eq 0 ]; then echo "  none staged"; else
  awk '{print $2}' /tmp/staged.txt | while read -r f; do
    [ "$(classify "$f")" = E ] && echo "  STAGED: $f"; done; fi
echo ""
echo "=== UNEXPLAINED (F) -- must be ZERO ==="
m=$(awk '{print $2}' /tmp/staged.txt | while read -r f; do
      [ "$(classify "$f")" = F ] && echo "$f"; done | wc -l)
if [ "$m" -eq 0 ]; then echo "  none staged"; else
  awk '{print $2}' /tmp/staged.txt | while read -r f; do
    [ "$(classify "$f")" = F ] && echo "  STAGED: $f"; done; fi

echo ""
echo "=== size of what is being added ==="
git diff --cached --numstat | awk '{a+=$1; d+=$2} END {printf "  +%d / -%d lines\n", a, d}'
echo "  bytes staged (new + changed blobs):"
git diff --cached --name-only | while read -r f; do [ -f "$f" ] && stat -c %s "$f"; done \
  | awk '{s+=$1} END {printf "    %.2f MB\n", s/1048576}'
echo "  largest staged files:"
git diff --cached --name-only | while read -r f; do
  [ -f "$f" ] && printf "%10d %s\n" "$(stat -c %s "$f")" "$f"; done \
  | sort -rn | head -6 | awk '{printf "    %8.1f KB  %s\n", $1/1024, $2}'

echo ""
echo "=== build artifacts / binaries staged? (must be none) ==="
git diff --cached --name-only | grep -iE '\.(o|obj|a|so|dll|exe|lib|pdb|sqlite|nsys-rep|qdrep)$|^build/|/CMakeFiles/|__pycache__' \
  && echo "  ^^ PROBLEM" || echo "  none"
nb=0
while IFS= read -r f; do
  [ -f "$f" ] || continue
  [ "$(file -b --mime-encoding "$f")" = binary ] && [ "$(stat -c %s "$f")" -gt 0 ] && {
    echo "  NON-EMPTY BINARY: $f ($(stat -c %s "$f") B)"; nb=$((nb+1)); }
done < <(git diff --cached --name-only)
echo "  non-empty binary files staged: $nb"

echo ""
echo "=== unstaged / untracked left behind (should be only ignored artifacts) ==="
git status --porcelain | grep -v '^[MARC]' | head -10
echo "  leftover entries: $(git status --porcelain | grep -vc '^[MARC]')"

echo ""
[ "$n" -eq 0 ] && [ "$m" -eq 0 ] && [ "$nb" -eq 0 ] \
  && echo "PHASE 5 DIFF REVIEW: PASS" || echo "PHASE 5 DIFF REVIEW: PROBLEMS PRESENT"
[ "$n" -eq 0 ] && [ "$m" -eq 0 ] && [ "$nb" -eq 0 ]

#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "=== 12 largest untracked files ==="
git ls-files --others --exclude-standard -z | xargs -0 du -b 2>/dev/null \
  | sort -rn | head -12 | awk '{printf "%10.1f KB  %s\n", $1/1024, $2}'
echo ""
echo "=== total untracked size ==="
git ls-files --others --exclude-standard -z | xargs -0 du -cb 2>/dev/null | tail -1 \
  | awk '{printf "%.2f MB across %s\n", $1/1048576, "'"$(git ls-files --others --exclude-standard | wc -l)"'"}'
echo ""
echo "=== untracked by extension ==="
git ls-files --others --exclude-standard | sed 's/.*\.//' | sort | uniq -c | sort -rn | head -15
echo ""
echo "=== any BINARY among the untracked? (file(1) says not text) ==="
n=0
while IFS= read -r f; do
  t=$(file -b --mime-encoding "$f" 2>/dev/null)
  if [ "$t" = "binary" ]; then echo "  BINARY: $f"; n=$((n+1)); fi
done < <(git ls-files --others --exclude-standard)
echo "  binary files: $n"
echo ""
echo "=== anything that looks like a build artifact / cache / object ==="
git ls-files --others --exclude-standard \
  | grep -iE '\.(o|obj|a|so|dll|exe|lib|pdb|ilk|ninja|cmake|stamp|d)$|/build/|/CMakeFiles/|__pycache__|\.egg-info' \
  || echo "  none"
echo ""
echo "=== stray files at the repo root (tracked state) ==="
for f in cmake_test_discovery_*.json; do
  [ -e "$f" ] || continue
  if git ls-files --error-unmatch "$f" > /dev/null 2>&1; then s=TRACKED
  elif git check-ignore -q "$f"; then s=ignored
  else s=UNTRACKED-NOT-IGNORED; fi
  echo "  $s  $f"
done | sort | uniq -c | sed 's/^/  /'

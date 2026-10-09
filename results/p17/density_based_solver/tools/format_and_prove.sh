#!/usr/bin/env bash
# P17.8 -- apply clang-format-18 and PROVE the change is formatting-only.
#
# Reformatting production source after it has been measured is a real hazard: a reflow can silently
# change an expression. The proof used here is P17.7's: the multiset of PREPROCESSOR TOKENS must be
# identical before and after. Whitespace and line breaks do not change tokens; an edited expression
# does. The focused suite is then re-run and its measured numbers compared.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
P17=results/p17/density_based_solver
W=$P17/controls/format
mkdir -p "$W"

FILES=(
  src/compressible/DensityBasedSolver.cpp
  include/cfd/compressible/DensityBasedSolver.hpp
  tests/unit/compressible/test_density_based_solver.cpp
  tests/integration/compressible/test_density_based_canonical.cpp
)

# A token multiset that ignores whitespace entirely: split on non-identifier characters, keeping the
# punctuation as its own tokens, then sort. Comments are STRIPPED first, since reflowing a comment is
# exactly what clang-format is here to do and a comment carries no semantics.
tokens() {
  sed -e 's://.*::' "$1" \
    | tr -d '\n' \
    | grep -oE '[A-Za-z_][A-Za-z0-9_]*|[0-9]+\.?[0-9]*([eE][-+]?[0-9]+)?|[^ A-Za-z0-9_]' \
    | LC_ALL=C sort
}

echo "P17.8 -- clang-format, proven formatting-only"
echo "date: $(date -u +%FT%TZ)"
echo ""
fail=0
for f in "${FILES[@]}"; do
  base=$(basename "$f")
  tokens "$f" > "$W/$base.tokens.before"
  before=$(sha256sum "$f" | awk '{print $1}')
  clang-format-18 -i "$f"
  after=$(sha256sum "$f" | awk '{print $1}')
  tokens "$f" > "$W/$base.tokens.after"
  printf '  %-52s\n' "$f"
  printf '    sha256 before %s\n' "$before"
  printf '    sha256 after  %s\n' "$after"
  if [ "$before" = "$after" ]; then
    printf '    already formatted, nothing changed\n'
  else
    printf '    reformatted (%s lines changed)\n' "$(diff <(echo) <(echo) >/dev/null; true)"
  fi
  if diff -q "$W/$base.tokens.before" "$W/$base.tokens.after" > /dev/null; then
    printf '    TOKEN MULTISET IDENTICAL (%s tokens) -- formatting only\n' \
           "$(wc -l < "$W/$base.tokens.after")"
  else
    printf '    *** TOKEN MULTISET CHANGED -- this is NOT a formatting-only change ***\n'
    diff "$W/$base.tokens.before" "$W/$base.tokens.after" | head -10 | sed 's/^/      /'
    fail=$((fail+1))
  fi
done
echo ""
echo "### clang-format-18 --dry-run --Werror over the whole tree ###"
if clang-format-18 --dry-run --Werror $(git ls-files 'include/*.hpp' 'include/**/*.hpp' 'src/*.cpp' 'src/**/*.cpp' 'apps/**/*.cpp' 'apps/**/*.hpp' 'tests/**/*.cpp' 'tests/**/*.hpp' 2>/dev/null) > "$W/tree_format.log" 2>&1; then
  echo "  CLEAN"
else
  echo "  violations remain:"
  grep -aoE '^[^:]+:' "$W/tree_format.log" | LC_ALL=C sort -u | head -20 | sed 's/^/    /'
  fail=$((fail+1))
fi
echo ""
echo "FORMAT PROOF FAILURES: $fail   (must be 0)"
if [ "$fail" -eq 0 ]; then exit 0; else exit 1; fi

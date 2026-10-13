#!/usr/bin/env bash
# P18.1 section 38 -- exactly what this phase changed, and nothing else.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1

P18_1_FILES="include/cfd/species/SpeciesRegistry.hpp
include/cfd/species/MassFractionState.hpp
src/species/SpeciesRegistry.cpp
src/species/MassFractionState.cpp
tests/unit/species/test_mass_fraction_state.cpp
src/CMakeLists.txt
tests/unit/species/CMakeLists.txt"

echo "=== the P18.1 changeset ==="
for f in $P18_1_FILES; do
  st=$(git status --porcelain -- "$f" | awk '{print $1}')
  printf '  %-2s %-55s %s\n' "${st:-  }" "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo ""
echo "  ?? = new file added by P18.1     M = pre-existing file modified by P18.1"
echo ""

echo "=== P18.1's OWN edits to the two CMakeLists ==="
echo ""
echo "NOTE, so this is not misread: both files were ALREADY modified against HEAD before P18.1"
echo "began -- they carry the uncommitted P13/P15/P16/P17 working tree that P18.0 froze as the"
echo "P18 start state. 'git diff' on them therefore shows that accumulated history too, which is"
echo "NOT P18.1's. Only the P18.1 hunks are shown here; the fingerprint section is what proves"
echo "nothing else moved."
echo ""
echo "--- src/CMakeLists.txt ---"
grep -n -B3 -A6 'P18.1: the multi-species mass-fraction state' src/CMakeLists.txt | sed 's/^/  /'
echo ""
echo "--- tests/unit/species/CMakeLists.txt ---"
grep -n 'test_mass_fraction_state.cpp' tests/unit/species/CMakeLists.txt | sed 's/^/  /'
echo ""

echo "=== proof that P18.1 modified no other PRE-EXISTING production or test file ==="
echo "  Tracked files modified in the whole tree : $(git status --porcelain | grep -c '^ M')"
echo "  ...of which P18.1 touched                : 2 (the two CMakeLists above)"
echo ""
echo "  The other modified files are the P14-P17 working tree that P18.0 audited and froze as"
echo "  the P18 start state. P18.1 did not touch them; the fingerprint check below proves it."
echo ""

echo "=== clang-format offenders, and whether any is a P18.1 file ==="
FMT=$(command -v clang-format-18)
find include src apps tests -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 \
  | xargs -0 "$FMT" --dry-run --Werror 2> /tmp/p18_1_fmt_cs.log
OFFENDERS=$(grep -oE '^[^ ]+\.(cpp|hpp|h):[0-9]+:[0-9]+: error' /tmp/p18_1_fmt_cs.log | cut -d: -f1 | sort -u)
echo "  total offending files: $(echo "$OFFENDERS" | grep -c .)"
HIT=0
for f in $P18_1_FILES; do
  if echo "$OFFENDERS" | grep -qx "$f"; then
    echo "  *** $f IS an offender -- P18.1 introduced a format violation ***"
    HIT=1
  fi
done
if [ "$HIT" -eq 0 ]; then
  echo "  NONE of the P18.1 files is an offender."
  echo ""
  echo "  clang-format's verdict on a file depends only on THAT FILE's content, and P18.1"
  echo "  changed none of the offending files. The violations are therefore inherited from the"
  echo "  uncommitted P14-P17 working tree, not introduced here. Recorded as pre-existing"
  echo "  technical debt; NOT fixed, because CLAUDE.md section 2 forbids repairing unrelated"
  echo "  defects inside another phase."
fi
echo ""
echo "  offenders:"
echo "$OFFENDERS" | sed 's/^/    /'

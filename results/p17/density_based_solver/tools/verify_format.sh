#!/usr/bin/env bash
# P17.8 -- verify that the clang-format pass changed FORMATTING ONLY, and that the tree is clean
# under the project's OWN format command.
#
# The first attempt at this proof (logs/12_format.log, preserved) used a raw token multiset and
# reported three files as semantically changed. That verdict was WRONG, and the reason is worth
# recording: clang-format splits a long string literal into two ADJACENT literals, which C++
# concatenates -- the program is identical but the count of `"` characters rises. Comparing the
# delimiters instead of the string CONTENTS is what produced the false alarm.
#
# prove_format_only.py compares two things instead: the token multiset OUTSIDE string literals, and
# the concatenation of all string-literal contents in order. Splitting a literal leaves both
# invariant; changing one character inside a message breaks the second.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
P17=results/p17/density_based_solver
W=$P17/controls/format
fail=0

echo "P17.8 -- clang-format verification"
echo "date: $(date -u +%FT%TZ)"
echo ""

echo "### 1. the PRODUCTION source: full two-part proof ###"
echo "  The reference is the file as it stood BEFORE the clang-format pass. Since that pass, ONE"
echo "  deliberate semantic change was made: the dead local \`totalVolume\` (assigned in its own"
echo "  loop, read nowhere) was removed from solve(). So the comparison below is NOT expected to"
echo "  report \"formatting only\" -- it is expected to report EXACTLY that deletion and nothing"
echo "  else. A precise, falsifiable statement is more useful here than a vague one."
PRE=$P17/controls/format/DensityBasedSolver.cpp.before_format_and_cleanup
POST=src/compressible/DensityBasedSolver.cpp
if [ ! -f "$PRE" ]; then
  echo "  *** the pre-format copy is missing, so nothing below is verified. FAILING CLOSED."
  fail=$((fail+1))
else
  echo "  pre-format  $PRE"
  echo "    sha256 $(sha256sum "$PRE" | awk '{print $1}')   (saved by controls.sh BEFORE formatting)"
  echo "  post-format $POST"
  echo "    sha256 $(sha256sum "$POST" | awk '{print $1}')"
  echo "  line count $(wc -l < "$PRE") -> $(wc -l < "$POST")"
  if python3 "$P17/tools/prove_format_only.py" "$PRE::$POST" > "$W/solver_proof.log" 2>&1; then
    echo "    TOKENS AND STRING CONTENT IDENTICAL -- but the totalVolume deletion should have shown"
    echo "    up here. Either the deletion did not happen or the proof is blind. FAILING CLOSED."
    fail=$((fail+1))
  else
    sed 's/^/  /' "$W/solver_proof.log"
    echo "  the difference must be EXACTLY the removed dead local, and nothing else:"
    LOST=$(grep -oE "lost : \[.*\]" "$W/solver_proof.log" || echo "lost : []")
    echo "    $LOST"
    # totalVolume appeared twice as an identifier, in "Real totalVolume = 0.0;" and in the loop
    # body "totalVolume += mesh.cell(i).volume();". Removing those two lines also removes the
    # surrounding punctuation and the loop header tokens.
    if echo "$LOST" | grep -q "totalVolume"; then
      echo "    CONFIRMED: totalVolume is among the removed tokens"
    else
      echo "    *** totalVolume is NOT among the removed tokens -- the change is something else ***"
      fail=$((fail+1))
    fi
    if grep -q "STRING CONTENT CHANGED" "$W/solver_proof.log"; then
      echo "    *** A STRING LITERAL CHANGED -- a message or diagnostic was altered ***"
      fail=$((fail+1))
    else
      echo "    string-literal content UNCHANGED -- no message or diagnostic was altered"
    fi
    echo "    totalVolume occurrences in the current source: $(grep -c totalVolume "$POST")   (must be 0)"
    [ "$(grep -c totalVolume "$POST")" -eq 0 ] || fail=$((fail+1))
  fi
fi

echo ""
echo "  the two P17.7 files the STAR-ENERGY AMENDMENT edited, and therefore P17.8's to keep clean:"
for pair in "$W/HllcFlux.hpp.preformat::include/cfd/compressible/HllcFlux.hpp"             "$W/hllc_reference_data.hpp.preformat::tests/unit/compressible/hllc_reference_data.hpp"; do
  pre=${pair%%::*}; post=${pair##*::}
  echo "  $post"
  if [ ! -f "$pre" ]; then echo "    pre-format copy missing -- NOT VERIFIED"; fail=$((fail+1)); continue; fi
  echo "    sha256 $(sha256sum "$pre" | awk '{print $1}') -> $(sha256sum "$post" | awk '{print $1}')"
  if python3 "$P17/tools/prove_format_only.py" "$pre::$post"; then :; else fail=$((fail+1)); fi
done

echo ""
echo "  SANITY -- the proof must be able to FAIL. One character changed inside a string literal:"
TMP=$(mktemp /tmp/p17_8_fmt_XXXX.cpp)
sed '0,/must be finite and > 0/s/must be finite and > 0/must be finite and > 1/' "$POST" > "$TMP"
if [ "$(sha256sum "$TMP" | awk '{print $1}')" = "$(sha256sum "$POST" | awk '{print $1}')" ]; then
  echo "    *** THE SANITY PLANT DID NOT APPLY -- the demonstration would be vacuous ***"
  fail=$((fail+1))
elif python3 "$P17/tools/prove_format_only.py" "$TMP::$POST" > /dev/null 2>&1; then
  echo "    *** NOT DETECTED -- the proof is blind ***"
  fail=$((fail+1))
else
  echo "    detected, as required"
fi
rm -f "$TMP"
echo ""

echo "### 2. the other three files: the saved token multisets, with delimiters separated out ###"
echo "  No pre-format copy exists for these, so the check uses logs/12_format.log's saved token"
echo "  files. The claim verified is narrow and stated as such: the ONLY tokens that differ are"
echo "  string delimiters, which is the signature of literal splitting and of nothing else."
for base in DensityBasedSolver.hpp test_density_based_solver.cpp test_density_based_canonical.cpp; do
  B=$W/$base.tokens.before
  A=$W/$base.tokens.after
  if [ ! -f "$B" ] || [ ! -f "$A" ]; then
    echo "  $base: token files missing -- NOT VERIFIED"
    fail=$((fail+1))
    continue
  fi
  nb=$(grep -cv '^"$' "$B")
  na=$(grep -cv '^"$' "$A")
  qb=$(grep -c '^"$' "$B")
  qa=$(grep -c '^"$' "$A")
  if diff -q <(grep -v '^"$' "$B") <(grep -v '^"$' "$A") > /dev/null; then
    printf '  %-36s non-delimiter tokens IDENTICAL (%s); delimiters %s -> %s\n' "$base" "$na" "$qb" "$qa"
  else
    printf '  %-36s *** NON-DELIMITER TOKENS DIFFER (%s -> %s) ***\n' "$base" "$nb" "$na"
    fail=$((fail+1))
  fi
done
echo ""

echo "### 3. the project's OWN format command, verbatim from .github/workflows/ci.yml ###"
echo "  find include src apps tests \\( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \\) -print0 |"
echo "  xargs -0 clang-format-18 --dry-run --Werror"
if find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 \
   | xargs -0 clang-format-18 --dry-run --Werror > "$W/ci_format.log" 2>&1; then
  echo "  CLEAN -- 0 violations"
else
  echo "  *** violations ***"
  grep -aoE '^[^:]+\.(cpp|hpp|h):' "$W/ci_format.log" | LC_ALL=C sort -u > "$W/violating_files.txt"
  echo "    files with violations: $(wc -l < "$W/violating_files.txt")"
  echo "    OF THOSE, any that P17.8 added or edited (these would be P17.8's to fix):"
  P17_8_FILES='DensityBasedSolver|test_density_based|HllcFlux.hpp|hllc_reference_data'
  mine=$(grep -cE "$P17_8_FILES" "$W/violating_files.txt" || true)
  echo "      $mine   (must be 0)"
  grep -E "$P17_8_FILES" "$W/violating_files.txt" | sed 's/^/        /'
  if [ "$mine" -ne 0 ]; then fail=$((fail+1)); fi
  echo "    the remainder are PRE-EXISTING, in files P17.8 does not touch. P17.7's own close"
  echo "    scoped its format claim to \"all 5 hand-written files\" rather than the tree, so the"
  echo "    tree has not been clean for some time. Recorded as technical debt, NOT fixed here:"
  echo "    CLAUDE.md section 2 forbids fixing unrelated defects inside another phase."
  sed 's/^/        /' "$W/violating_files.txt"
fi
echo ""
echo "FORMAT VERIFICATION FAILURES: $fail   (must be 0)"
if [ "$fail" -eq 0 ]; then exit 0; else exit 1; fi

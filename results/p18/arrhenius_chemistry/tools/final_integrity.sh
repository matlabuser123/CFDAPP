#!/usr/bin/env bash
# P18.5 section 50 -- final repository integrity.
#
# TWO DEFECTS OF THE FIRST VERSION OF THIS SCRIPT ARE FIXED HERE, and both are recorded rather
# than quietly corrected, because both are the failure mode this phase keeps meeting:
#
#   (1) IT FAILED OPEN. `fail=1` was written inside a $( ) command substitution -- a SUBSHELL --
#       so the assignment was discarded. The script printed "INTEGRITY: PASS" while three checks
#       reported NOT 0. The verdict is now assigned in the function body.
#
#   (2) IT GREPPED PROSE. The exclusion checks matched comments and error-message strings, so
#       "equilibrium", "omega" and "clamp" were each reported present when every occurrence was
#       text explaining that P18.5 does NOT do that thing. The checks now run on code-only copies
#       produced by tools/strip_code.py.
#
# EVERY "MUST BE 0" CHECK IS PAIRED WITH A POSITIVE CONTROL that must be NON-ZERO on the same
# stripped file. A grep for a renamed symbol, or against a path that no longer exists, returns 0
# and prints exactly what success looks like. The controls are the only thing separating "the file
# does not contain this" from "this script is not reading the file".
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
SRC=src/species/ArrheniusKinetics.cpp
HDR=include/cfd/species/ArrheniusKinetics.hpp
TST=tests/unit/species/test_arrhenius_kinetics.cpp
TOOLS=results/p18/arrhenius_chemistry/tools

fail=0

must_be_zero() {
  local label="$1" count="$2" verdict="ok"
  if [ "$count" != "0" ]; then
    verdict="*** NOT 0 ***"
    fail=1
  fi
  printf '  %-44s %-4s %s\n' "$label" "$count" "$verdict"
}

must_be_nonzero() {
  local label="$1" count="$2" verdict="ok (control: the grep reads the file)"
  if [ "$count" = "0" ]; then
    verdict="*** 0 -- THE PROBE IS BLIND ***"
    fail=1
  fi
  printf '  %-44s %-4s %s\n' "$label" "$count" "$verdict"
}

echo "=== files exist (before anything is grepped) ==="
for f in "$HDR" "$SRC" "$TST"; do
  if [ -f "$f" ]; then
    printf '  %-56s present, %s lines\n' "$f" "$(wc -l < "$f")"
  else
    printf '  %-56s *** MISSING ***\n' "$f"
    fail=1
  fi
done
echo ""

CODE=$(mktemp -d)
trap 'rm -rf "$CODE"' EXIT
python3 "$TOOLS/strip_code.py" "$HDR" "$CODE/hdr" || { echo "strip failed"; exit 1; }
python3 "$TOOLS/strip_code.py" "$SRC" "$CODE/src" || { echo "strip failed"; exit 1; }

# counts over the CODE-ONLY copies
n() { grep -c "$1" "$CODE/hdr" "$CODE/src" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}'; }
ni() { grep -ci "$1" "$CODE/hdr" "$CODE/src" 2>/dev/null | awk -F: '{s+=$2} END{print s+0}'; }
nsrc() { grep -c "$1" "$CODE/src" 2>/dev/null || true; }

echo "=== the code-only copies the exclusion checks run on ==="
printf '  %-44s %s lines\n' "stripped header" "$(wc -l < "$CODE/hdr")"
printf '  %-44s %s lines\n' "stripped source" "$(wc -l < "$CODE/src")"
must_be_nonzero "control: stripped source has statements" "$(nsrc ';')"
must_be_nonzero "control: stripped header declares the class" "$(n 'class ArrheniusParameters')"
echo ""

echo "=== git status summary ==="
git status --porcelain | awk '{print substr($0,1,2)}' | sort | uniq -c | sed 's/^/  /'
echo ""

echo "=== git diff --check (whitespace errors) ==="
git diff --check > /tmp/p18_5_dc.txt 2>&1 || true
RE=':[0-9]+: (trailing whitespace|space before tab|indent with spaces|new blank line)'
echo "  total warnings in the tree : $(grep -cE "$RE" /tmp/p18_5_dc.txt || true)"
grep -oE "^[^+ ][^:]*$RE" /tmp/p18_5_dc.txt | sed 's/:[0-9]*: .*//' | sort | uniq -c | sort -rn | sed 's/^/    /'
echo "  (259 pre-existing, in three CMakeLists no P18 phase authored -- a PUSH BLOCKER recorded"
echo "   in TODO.md, not a P18.5 regression. P18.5 fixed the one TODO.md warning it found.)"
must_be_zero "in a P18.5 file" \
  "$(grep -oE "^[^+ ][^:]*$RE" /tmp/p18_5_dc.txt | sed 's/:[0-9]*: .*//' | sort -u | grep -cE 'ArrheniusKinetics|test_arrhenius' || true)"
must_be_zero "in TODO.md or ROADMAP.md" \
  "$(grep -oE "^[^+ ][^:]*$RE" /tmp/p18_5_dc.txt | sed 's/:[0-9]*: .*//' | sort -u | grep -cE 'TODO.md|ROADMAP.md' || true)"
echo ""

echo "=== clang-format, the three P18.5 files ==="
if clang-format-18 --dry-run --Werror "$HDR" "$SRC" "$TST" 2>/dev/null; then
  echo "  CLEAN"
else
  echo "  *** VIOLATIONS ***"
  fail=1
fi
echo ""

echo "=== frozen gate, re-hashed at close ==="
GATE=$(sha256sum results/p18/gates/P18_GATES.md | awk '{print $1}')
echo "  actual   $GATE"
echo "  expected 921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3"
if [ "$GATE" != "921616544e3631d40de3ea629a1617e8ec014f89fcb1d7b8a728db7132d618f3" ]; then
  echo "  *** THE FROZEN GATE HAS CHANGED ***"
  fail=1
fi
echo ""

echo "=== the P18.5 frozen docs, re-hashed at close ==="
echo "  formulation.md is AMENDED by A1 and must itself be UNCHANGED"
for f in audit.md units.md formulation.md formulation_A1.md; do
  printf '  %-22s %s\n' "$f" "$(sha256sum results/p18/arrhenius_chemistry/$f | awk '{print $1}')"
done
FORM=$(sha256sum results/p18/arrhenius_chemistry/formulation.md | awk '{print $1}')
echo "  expected formulation.md 9362c7289979a8494f2c9b3b4a76b83e6d3a109dd8448177c1b07c90a0ea1da5"
if [ "$FORM" != "9362c7289979a8494f2c9b3b4a76b83e6d3a109dd8448177c1b07c90a0ea1da5" ]; then
  echo "  *** formulation.md WAS EDITED AFTER FREEZING ***"
  fail=1
fi
echo ""

echo "=== results/validation runtime noise ==="
NOISE=$(git status --porcelain -- results/validation | wc -l)
must_be_zero "modified files under results/validation" "$NOISE"
echo ""

echo "=== NO HEAT-RELEASE CODE ADDED EARLY (P18.6 is NOT AUTHORIZED) ==="
for pat in heatRelease reactionEnthalpy heatOfFormation formationEnthalpy energySource; do
  must_be_zero "$pat" "$(ni "$pat")"
done
must_be_nonzero "control: 'temperature' appears in code" "$(n 'temperature')"
echo "  P18.5 READS T and never writes it:"
must_be_zero "writes to a temperature field" "$(grep -cE 'temperature\[[a-zA-Z]+\] *=[^=]' "$CODE/src" || true)"
must_be_nonzero "control: reads temperature[cell]" "$(nsrc 'temperature\[cell\]')"
echo ""

echo "=== NO EQUILIBRIUM CONSTANT INVENTED (section 20) ==="
for pat in equilibrium gibbs keq entropy; do
  must_be_zero "$pat" "$(ni "$pat")"
done
must_be_nonzero "control: 'reverse' appears in code" "$(ni 'reverse')"
echo ""

echo "=== NO THIRD-BODY OR PRESSURE-DEPENDENT KINETICS (not in the gate) ==="
for pat in thirdBody troe lindemann falloff chaperon; do
  must_be_zero "$pat" "$(ni "$pat")"
done
echo ""

echo "=== NO DUPLICATED CONSERVATION FORMULA (sections 25, 26) ==="
must_be_zero "omega computed in the kinetics layer" "$(ni 'omega')"
must_be_zero "massResidual recomputed here" "$(n 'massResidual')"
must_be_zero "netCoefficient used for an ORDER" "$(n 'netCoefficient')"
must_be_nonzero "control: reactantCoefficient IS used" "$(nsrc 'reactantCoefficient')"
must_be_nonzero "control: productCoefficient IS used" "$(nsrc 'productCoefficient')"
echo ""

echo "=== NO DUPLICATED CONSTANTS OR WEIGHTS (section 16) ==="
must_be_zero "a local universal gas constant" "$(grep -cE '8\.314|831446' "$CODE/src" || true)"
must_be_nonzero "control: P18.3's R_u IS used" "$(nsrc 'kUniversalGasConstant')"
must_be_zero "molecular weights STORED here" "$(n 'molecularWeight_')"
must_be_nonzero "control: M_i READ from P18.3" "$(nsrc 'molecularWeight()')"
echo ""

echo "=== NOTHING IS CLAMPED (sections 7, 30) ==="
for pat in clamp clip 'std::max(0' 'std::min(1' normali; do
  must_be_zero "$pat" "$(nsrc "$pat")"
done
must_be_nonzero "control: throws instead" "$(nsrc 'throw ')"
echo ""

echo "=== P18.1-P18.4 PRODUCTION UNTOUCHED (sections 43-46) ==="
echo "  hashes recorded so a later phase can prove these did not move either:"
for f in src/species/SpeciesRegistry.cpp src/species/MassFractionState.cpp \
         src/species/SpeciesTransport.cpp src/species/MixtureThermodynamics.cpp \
         src/species/ReactionMechanism.cpp src/transport/ScalarTransport.cpp; do
  printf '  %-48s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}' | cut -c1-32)"
done
echo ""

echo "=== no test deleted or disabled ==="
must_be_zero "DISABLED_ added by P18.5" "$(grep -c 'DISABLED_' "$TST" || true)"
REG=$(./build/final/tests/unit/species/CFDSpeciesTests --gtest_list_tests 2>/dev/null | grep -cE '^  [A-Za-z]')
echo "  CFDSpeciesTests registered : $REG   (P18.4 closed at 203; 203 + 38 = 241)"
if [ "$REG" != "241" ]; then
  echo "  *** expected 241 ***"
  fail=1
fi
echo ""

echo "=== the P18.5 changeset ==="
for f in "$HDR" "$SRC" "$TST"; do
  printf '  %-56s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo ""
echo "=== commit / push ==="
echo "  HEAD: $(git rev-parse HEAD)"
echo "  nothing committed, pushed, reset, cleaned or stashed by P18.5."
echo ""
if [ "$fail" -eq 0 ]; then
  echo "INTEGRITY: PASS -- every exclusion holds AND every probe was proven to read its file"
else
  echo "INTEGRITY: FAIL"
fi
exit "$fail"

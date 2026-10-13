#!/usr/bin/env bash
# P18.1 R1 -- correct a comment in compositionSum() that overclaims the mechanism, and PROVE the
# change is comment-only so the completed regression stays authoritative.
#
# The proof is not "I only edited a comment". It is: rebuild and show the emitted object file is
# BYTE-IDENTICAL. This repository's host toolchain is bit-reproducible (nvcc is not, but this is a
# host .cpp), so an identical object hash is a real proof that no code changed.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
BUILD=build/final
OBJ=$(find "$BUILD" -name 'MassFractionState.cpp.o' | head -1)

echo "=== before ==="
echo "  source : $(sha256sum src/species/MassFractionState.cpp | awk '{print $1}')"
echo "  object : $(sha256sum "$OBJ" | awk '{print $1}')"
cp "$OBJ" /tmp/p18_1_mfs_before.o
cp src/species/MassFractionState.cpp /tmp/p18_1_mfs_before.cpp
echo ""

python3 - <<'PY'
import pathlib
p = pathlib.Path('src/species/MassFractionState.cpp')
s = p.read_text()
old = """  // FIXED ORDER: ascending species index, accumulated left to right into
  // one Real. Nothing here may become a parallel reduction or a compensated
  // sum without changing the number this returns, and validate() must use
  // this same function so the two can never disagree."""
new = """  // FIXED ORDER: ascending species index, accumulated left to right into
  // one Real. Nothing here may become a parallel reduction or a compensated
  // sum without changing the number this returns.
  //
  // validateCell() accumulates its OWN sum, over the composition vector it
  // is handed rather than over fields_. The two agree because validate()
  // builds that vector from fields_ in this same ascending order
  // immediately before summing it, so both perform the identical sequence
  // of additions on the identical values -- but that is an agreement of
  // construction, NOT something this function enforces. Change the order in
  // either loop and the other must change with it."""
assert old in s, "target comment not found -- refusing to edit blindly"
p.write_text(s.replace(old, new))
print("comment replaced")
PY
echo ""

echo "=== the diff, which must contain no non-comment line ==="
diff /tmp/p18_1_mfs_before.cpp src/species/MassFractionState.cpp | sed 's/^/  /'
echo ""
NONCOMMENT=$(diff /tmp/p18_1_mfs_before.cpp src/species/MassFractionState.cpp \
             | grep -E '^[<>]' | grep -vE '^[<>] *(//|$)' | wc -l)
echo "  changed lines that are NOT comments: $NONCOMMENT"
[ "$NONCOMMENT" -eq 0 ] || { echo "  *** NOT COMMENT-ONLY -- the regression would be invalidated ***"; exit 1; }
echo ""

echo "=== format ==="
clang-format-18 --dry-run --Werror src/species/MassFractionState.cpp && echo "  clang-format: clean"
echo ""

echo "=== rebuild and compare the object ==="
rm -f "$OBJ"
cmake --build "$BUILD" --target cfdcore -j 20 > /tmp/p18_1_r1_build.log 2>&1
if [ ! -f "$OBJ" ]; then echo "  BUILD FAILED"; grep error: /tmp/p18_1_r1_build.log | head -5; exit 1; fi
echo "  source : $(sha256sum src/species/MassFractionState.cpp | awk '{print $1}')"
echo "  object : $(sha256sum "$OBJ" | awk '{print $1}')"
echo ""
if cmp -s /tmp/p18_1_mfs_before.o "$OBJ"; then
  echo "  OBJECT FILE IS BYTE-IDENTICAL."
  echo "  PROVED: the change emitted not one different instruction. The three-configuration"
  echo "  regression remains authoritative for this source."
else
  echo "  *** OBJECT DIFFERS -- this was not a comment-only change. The regression must be rerun. ***"
  exit 1
fi
echo ""
echo "=== focused tests, to close the loop ==="
rm -f "$BUILD/tests/unit/species/CFDSpeciesTests"
cmake --build "$BUILD" --target CFDSpeciesTests -j 20 > /dev/null 2>&1
"$BUILD/tests/unit/species/CFDSpeciesTests" 2>&1 | tail -3

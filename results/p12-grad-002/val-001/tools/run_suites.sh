#!/usr/bin/env bash
# VAL-001 step 11: fresh rebuild, then the named test, the complete grid-refinement suite, the
# gradient tests and the MMS tests. Executable-bit guard applied; a suite that cannot run is
# reported NOT RUN, never counted as passing.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/val-001
mkdir -p "$P/logs"
cd "$R" || exit 1
SUITES="CFDDiscretizationTests CFDMMSValidationTests CFDMeshTests CFDPhysicsTests"
cmake --build build/release -j"$(nproc)" $(for s in $SUITES; do echo --target "$s"; done) \
      > "$P/logs/$1.build" 2>&1 || { echo "BUILD FAILED"; tail -30 "$P/logs/$1.build"; exit 1; }
{
  echo "# VAL-001 step 11; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore.a $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo
  B=$(find build/release -type f -name CFDDiscretizationTests | head -1)
  chmod +x "$B"
  echo "## the named test, verbatim"
  ./"$B" --gtest_filter=GridRefinementTest.GreenGaussGradientDistortedGlobalOrderReflectsBoundaryTreatment 2>&1 \
    | grep -E "^(GreenGauss|Grid|8x8|16x16|32x32|64x64)|OK|FAILED|Failure"
  echo
  echo "## complete GridRefinementTest suite"
  ./"$B" --gtest_filter='GridRefinementTest.*' 2>&1 | grep -E '^\[==========\] [0-9]+ tests|^\[  PASSED  \]|^\[  FAILED  \]'
  echo
  echo "## gradient tests (any suite with Gradient in the name)"
  ./"$B" --gtest_filter='*Gradient*' 2>&1 | grep -E '^\[==========\] [0-9]+ tests|^\[  PASSED  \]|^\[  FAILED  \]'
  echo
  for s in $SUITES; do
    BIN=$(find build/release -type f -name "$s" 2>/dev/null | head -1)
    if [ -z "$BIN" ]; then echo "$s: NOT RUN -- binary not found"; continue; fi
    chmod +x "$BIN" 2>/dev/null
    if [ ! -x "$BIN" ]; then echo "$s: NOT RUN -- not executable"; continue; fi
    echo "## $s (binary $(sha256sum "$BIN" | cut -c1-16))"
    OUT=$(./"$BIN" 2>&1)
    echo "$OUT" | grep -E '^\[==========\] [0-9]+ tests from|^\[  PASSED  \]|^\[  FAILED  \] [0-9]+ test'
    echo "$OUT" | grep -E '^\[  FAILED  \] [A-Za-z]' | sed 's/^/    /'
  done
} > "$P/logs/$1" 2>&1
cat "$P/logs/$1"

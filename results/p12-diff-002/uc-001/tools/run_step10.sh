#!/usr/bin/env bash
# P12-DIFF-002-UC-001 Step 10: FRESH full rebuild, then the suites the authorization names
# (Poiseuille / momentum / boundary diffusion / SIMPLE / MMS) plus the W7 focused suite list.
# No threshold and no golden output is modified by this script.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uc-001
mkdir -p "$P/logs"
cd "$R" || exit 1

echo "== fresh rebuild =="
cmake --build build/release -j"$(nproc)" --clean-first > "$P/logs/09_rebuild.log" 2>&1
RC=$?
echo "rebuild exit $RC ($(wc -l < "$P/logs/09_rebuild.log") lines)"
if [ $RC -ne 0 ]; then tail -40 "$P/logs/09_rebuild.log"; exit 1; fi
LIBSHA=$(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)
echo "libcfdcore.a sha256 $LIBSHA"

SUITES="CFDPoiseuilleValidationTests CFDDiscretizationTests CFDPhysicsTests CFDPisoTests
        CFDSimpleTests CFDMMSValidationTests CFDMeshTests CFDSolverTests CFDCoreTests
        CFDFieldTests CFDAlgebraTests CFDThermalTests CFDTurbulenceTests
        CFDCaseIntegrationTests"
{
  echo "# UC-001 Step 10 verification; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore.a sha256 $LIBSHA (fresh --clean-first rebuild)"
  for s in $SUITES; do
    BIN=$(find build/release -type f -name "$s" -perm -u+x 2>/dev/null | head -1)
    if [ -z "$BIN" ]; then echo "$s: NOT BUILT"; continue; fi
    OUT=$("$BIN" 2>&1)
    echo "$s: $(echo "$OUT" | grep -E '^\[==========\] [0-9]+ tests from|^\[  PASSED  \]|^\[  FAILED  \] [0-9]+ test' | tr '\n' ' ')"
    echo "$OUT" | grep -E '^\[  FAILED  \] [A-Za-z]' | sed 's/^/    /'
  done
} > "$P/logs/10_step10_suites.log" 2>&1
cat "$P/logs/10_step10_suites.log"

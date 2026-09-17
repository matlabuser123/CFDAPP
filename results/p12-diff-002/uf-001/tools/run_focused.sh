#!/usr/bin/env bash
# UF-001.13: the focused suites the authorization names -- natural convection, heated cavity,
# thermal, momentum, boundary diffusion, MMS and conservation.
# Freshly linked binaries on the Windows mount can come out WITHOUT the executable bit, so each
# one is chmod'ed and its existence asserted; a suite that cannot be executed is reported as
# NOT RUN, never silently counted as passing.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uf-001
mkdir -p "$P/logs"
cd "$R" || exit 1

SUITES="CFDNaturalConvectionValidationTests CFDHeatedCavityValidationTests
        CFDConjugateHeatTransferValidationTests CFDBoussinesqCouplingTests CFDThermalTests
        CFDPhysicsTests CFDDiscretizationTests CFDMMSValidationTests
        CFDSpeciesConservationValidationTests CFDMultiphaseConservationValidationTests
        CFDCavityValidationTests"

cmake --build build/release -j"$(nproc)" $(for s in $SUITES; do echo --target "$s"; done) \
      > "$P/logs/$1.build" 2>&1 || { echo "BUILD FAILED"; tail -30 "$P/logs/$1.build"; exit 1; }

{
  echo "# UF-001.13 focused suites; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore.a $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  for s in $SUITES; do
    BIN=$(find build/release -type f -name "$s" 2>/dev/null | head -1)
    if [ -z "$BIN" ]; then echo "$s: NOT RUN -- binary not found"; continue; fi
    chmod +x "$BIN" 2>/dev/null
    if [ ! -x "$BIN" ]; then echo "$s: NOT RUN -- binary not executable"; continue; fi
    echo "## $s  (binary $(sha256sum "$BIN" | cut -c1-16))"
    OUT=$(./"$BIN" 2>&1)
    echo "$OUT" | grep -E '^\[==========\] [0-9]+ tests from|^\[  PASSED  \]|^\[  FAILED  \] [0-9]+ test'
    echo "$OUT" | grep -E '^\[  FAILED  \] [A-Za-z]' | sed 's/^/    /'
  done
} > "$P/logs/$1" 2>&1
cat "$P/logs/$1"

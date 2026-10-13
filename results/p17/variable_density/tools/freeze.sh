#!/usr/bin/env bash
# P17.1 gate freeze record -- CLAUDE.md section 3 step 8.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/variable_density
cd "$ROOT" || exit 1
mkdir -p "$P17/logs"
B=build/qual-final
{
echo "P17.1 GATE FREEZE"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo ""
echo "### gate + design documents ###"
sha256sum "$P17/acceptance_gate.md" "$P17/design.md" "$P17/tools/dryrun.cpp" \
          "$P17/tools/run_dryrun.sh" | sed 's|/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/||'
echo ""
echo "### production files P17.1 may touch, BEFORE the change ###"
sha256sum src/compressible/CompressibleMassFlux.cpp \
          include/cfd/compressible/CompressibleMassFlux.hpp
echo ""
echo "### test file P17.1 will extend, BEFORE ###"
sha256sum tests/unit/compressible/test_compressible_mass_flux.cpp
echo ""
echo "### libraries under test, BEFORE ###"
sha256sum "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" 2>/dev/null | sed "s|$B/||"
echo ""
echo "### fingerprints, BEFORE ###"
echo "  P17_CODE_FINGERPRINT (P17.0 baseline): 1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2"
echo "  SOURCE_FINGERPRINT_V2 (P17.0 freeze) : fbf786656eac2284956a4e29fe2745157a3e59ab339833a9f49ff6e048bd9fd6"
echo ""
echo "### baseline regression counts P17.1 must not reduce ###"
echo "  Release+CUDA  2497 executed / 0 failed"
echo "  CPU-only      2297 executed / 0 failed"
echo ""
echo "### baseline test counts for the file being extended ###"
echo "  CompressibleMassFluxTest registered: $(ctest --test-dir $B -N -R CompressibleMassFluxTest 2>/dev/null | grep -aoE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')"
echo "  TEST macros in test_compressible_mass_flux.cpp: $(grep -cE '^TEST' tests/unit/compressible/test_compressible_mass_flux.cpp)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$P17/logs/00_freeze.log"

#!/usr/bin/env bash
# P14 migration, post-deletion sweep (gate step 11).
#
# EXPECTED FINAL STATE
#   assembleScalarTransportEquation -> resolves ONLY to the canonical
#                                      cfd::transport implementation, its own
#                                      tests and its documentation. Any hit in
#                                      src/turbulence or include/cfd/turbulence
#                                      is an orphaned duplicate and a FAILURE.
#   ScalarAssemblyRoute             -> no hits anywhere. The migration switch is
#                                      temporary scaffolding and must not ship.
#   LegacyMatrixEquivalence         -> no hits in tests/. Retired with the second
#                                      implementation it compared against.
#                                      Hits in results/ are EVIDENCE and expected.
#
# Every hit is printed for inspection; this script reports, it does not judge.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

hits() {
  local pattern="$1"; shift
  echo "=============================================================="
  echo "PATTERN: $pattern"
  echo "=============================================================="
  grep -RIn "$pattern" "$@" 2>/dev/null || echo "  (no hits)"
  echo
}

hits "assembleScalarTransportEquation" include src tests apps
hits "ScalarAssemblyRoute"             include src tests apps
hits "LegacyMatrixEquivalence"         include src tests apps
# The two legacy contribution loops by name, in the turbulence namespace only.
echo "=============================================================="
echo "turbulence-namespace assembly symbols (expect: none)"
echo "=============================================================="
grep -RIn "assembleScalarDiffusionContribution\|applyImplicitScalarSource" \
  include/cfd/turbulence src/turbulence 2>/dev/null || echo "  (no hits)"

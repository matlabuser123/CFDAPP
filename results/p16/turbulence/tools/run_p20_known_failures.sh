#!/usr/bin/env bash
# =====================================================================
# KNOWN EXPECTED FAILURES -- P20 scope, NOT part of the green P16 regression.
# =====================================================================
#
# These tests are deliberately kept, deliberately NOT disabled, and deliberately
# NOT part of the P16 regression. They assert the adopted attached-flow gates
#
#     E_Re_tau <= 0.059   AND   E_u+,Linf <= 1.4
#
# against the three deferred automatic wall treatments, all of which are
# documented to fail them:
#
#   automatic               attempt 2, 4th-power blend of two friction velocities
#   automatic_spalding      attempt 4, Spalding composite law
#   automatic_momentum_flux attempt 5, F_U = -rho u_tau u*
#
# Expected result: RED. A GREEN result here is itself a finding -- it would mean
# something changed the attached-flow behaviour of a deferred mode, and the
# numbers should be compared against
# results/p16/turbulence/automatic_wall/SUMMARY.md before anything is concluded.
#
# The P16 regression excludes exactly these suites, by the matching filter in
# run_p14_regression.sh. Nothing is hidden: this command is how they stay
# observable.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
B=./build/final/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests
[ -x "$B" ] || { echo "not built -- run a channel build first"; exit 1; }
echo "=== P20 KNOWN EXPECTED FAILURES (red is the expected outcome) ==="
"$B" --gtest_filter='SSTAutomaticWallChannel.*:SSTCompositeWallChannel.*:SSTMomentumFluxChannel.*:WallArchitectureDiagnostic.*' \
  2>&1 | grep -v "^\[INFO\]" | tail -40

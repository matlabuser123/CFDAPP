#!/usr/bin/env bash
# P16 step 4 -- targeted production verification after the wiring.
#
# Binaries are DELETED before the build and their existence checked after, so a
# stale artifact can never be mistaken for a passing run.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH

TURB=./build/final/tests/unit/turbulence/CFDTurbulenceTests
SOLVER=./build/final/tests/unit/solver/CFDSolverTests
PHYS=./build/final/tests/unit/physics/CFDPhysicsTests
CHAN=./build/final/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests

rm -f "$TURB" "$SOLVER" "$PHYS" "$CHAN"
echo "=== building every affected target ==="
cmake --build build/final -j 16 --target CFDTurbulenceTests CFDSolverTests CFDPhysicsTests \
  CFDTurbulenceChannelValidationTests 2>&1 | grep -E "error:|FAILED|unknown target" | head -20
echo "--- built ---"

run() {
  local label=$1 binary=$2
  if [ ! -x "$binary" ]; then echo "  $label: *** BINARY MISSING -- BUILD FAILED ***"; return 1; fi
  "$binary" > /tmp/p16_run.log 2>&1
  local rc=$?
  echo "  $label: exit $rc  $(grep -E 'PASSED|FAILED' /tmp/p16_run.log | tr '\n' ' ')"
}
echo
echo "=== 1. turbulence unit ==="          ; run turbulence "$TURB"
echo "=== 2. solver (SIMPLE / momentum) ==="; run solver     "$SOLVER"
echo "=== 3. physics (momentum assembly) ==="; run physics   "$PHYS"
echo "=== 4. channel-flow validation (pre-P16 thresholds, must be UNCHANGED) ==="
run channel "$CHAN"
echo
echo "=== 5. deterministic repeat ==="
"$TURB" > /tmp/p16_a.log 2>&1; "$TURB" > /tmp/p16_b.log 2>&1
if diff -q <(grep -E "^\[       OK \]" /tmp/p16_a.log) <(grep -E "^\[       OK \]" /tmp/p16_b.log) >/dev/null; then
  echo "  identical OK lists across two runs: YES"
else
  echo "  identical OK lists across two runs: NO"
fi

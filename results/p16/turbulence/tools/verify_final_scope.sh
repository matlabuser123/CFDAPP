#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
T=./build/final/tests/unit/turbulence/CFDTurbulenceTests
S=./build/final/tests/unit/solver/CFDSolverTests
P=./build/final/tests/unit/physics/CFDPhysicsTests
I=./build/final/tests/unit/io/CFDIoTests
C=./build/final/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests
rm -f "$T" "$S" "$P" "$I" "$C"
cmake --build build/final -j 16 --target CFDTurbulenceTests CFDSolverTests CFDPhysicsTests \
  CFDIoTests CFDTurbulenceChannelValidationTests 2>&1 | grep -E "error:|FAILED" | head -20
for pair in "turbulence:$T" "solver:$S" "physics:$P" "io:$I" "channel:$C"; do
  label=${pair%%:*}; bin=${pair#*:}
  if [ ! -x "$bin" ]; then echo "  $label: *** MISSING ***"; continue; fi
  "$bin" > /tmp/p16_f.log 2>&1
  echo "  $label: exit $? $(grep -E 'PASSED|FAILED' /tmp/p16_f.log | tr '\n' ' ')"
done

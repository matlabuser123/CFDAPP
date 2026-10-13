#!/usr/bin/env bash
# P16 k-epsilon closeout verification. Binaries deleted before build.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
T=./build/final/tests/unit/turbulence/CFDTurbulenceTests
S=./build/final/tests/unit/solver/CFDSolverTests
P=./build/final/tests/unit/physics/CFDPhysicsTests
I=./build/final/tests/unit/io/CFDIoTests
rm -f "$T" "$S" "$P" "$I"
cmake --build build/final -j 16 --target CFDTurbulenceTests CFDSolverTests CFDPhysicsTests \
  CFDIoTests 2>&1 | grep -E "error:|FAILED" | head -20
for pair in "turbulence:$T" "solver:$S" "physics:$P" "io:$I"; do
  label=${pair%%:*}; bin=${pair#*:}
  if [ ! -x "$bin" ]; then echo "  $label: *** MISSING ***"; continue; fi
  "$bin" > /tmp/p16_c.log 2>&1
  echo "  $label: exit $? $(grep -E 'PASSED|FAILED' /tmp/p16_c.log | tr '\n' ' ')"
done

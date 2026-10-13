#!/usr/bin/env bash
# P14 migration: the non-channel regression scope.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
TARGETS="CFDTransportTests CFDTurbulenceTests CFDIoTests CFDSolverTests CFDPhysicsTests"
cmake --build build/final --target $TARGETS -j 16 2>&1 | grep -E "error:|FAILED" | head -20
for t in transport turbulence io solver physics; do
  B=$(find ./build/final/tests/unit/$t -maxdepth 1 -name 'CFD*Tests' -type f | head -1)
  [ -x "$B" ] || { echo "$t: NOT BUILT"; continue; }
  printf '%-12s %s\n' "$t" "$("$B" 2>&1 | tail -3 | tr '\n' ' ')"
done

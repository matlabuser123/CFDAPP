#!/usr/bin/env bash
# P16 step 9 -- the regression scope affected by the SST work.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
clang-format-18 --style=file -i tests/integration/turbulence_channel/test_wall_function_channel.cpp
T=./build/final/tests/unit/turbulence/CFDTurbulenceTests
S=./build/final/tests/unit/solver/CFDSolverTests
P=./build/final/tests/unit/physics/CFDPhysicsTests
I=./build/final/tests/unit/io/CFDIoTests
C=./build/final/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests
B=./build/final/tests/integration/backward_facing_step/CFDBackwardFacingStepTests
rm -f "$T" "$S" "$P" "$I" "$C" "$B"
cmake -S . -B build/final > /dev/null 2>&1
cmake --build build/final -j 16 --target CFDTurbulenceTests CFDSolverTests CFDPhysicsTests \
  CFDIoTests CFDTurbulenceChannelValidationTests CFDBackwardFacingStepTests 2>&1 \
  | grep -E "error:|FAILED" | head -20
for pair in "turbulence:$T" "solver:$S" "physics:$P" "io:$I" "channel:$C" "step:$B"; do
  label=${pair%%:*}; bin=${pair#*:}
  if [ ! -x "$bin" ]; then echo "  $label: *** MISSING ***"; continue; fi
  "$bin" > /tmp/p16_s.log 2>&1
  echo "  $label: exit $? $(grep -E 'PASSED|FAILED' /tmp/p16_s.log | tr '\n' ' ')"
done

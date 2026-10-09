#!/usr/bin/env bash
# P17.11 sections 6-15 -- capture the QUANTITATIVE metrics each gate needs, from the FINAL tree.
#
# ctest hides the stdout of passing tests, so the numbers the gates ask for (closure errors, mass
# imbalance, jump ratios, max CPU/GPU differences) are not in a ctest log. The gtest binaries print
# them, so they are driven directly here and their full output preserved per gate.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
Q=results/p17/qualification
T=build/qual-final

run() {  # run <log-name> <binary> <gtest filter>
  local name="$1" bin="$2" filt="$3"
  if [ ! -x "$T/$bin" ]; then echo "  MISSING BINARY: $bin"; return 1; fi
  OMP_NUM_THREADS=1 "$T/$bin" --gtest_filter="$filt" > "$Q/logs/30_$name.log" 2>&1
  local rc=$?
  local p f
  p=$(grep -acE '^\[       OK \]' "$Q/logs/30_$name.log")
  f=$(grep -acE '^\[  FAILED  \]' "$Q/logs/30_$name.log")
  printf '  %-28s exit=%d  ok=%-4s failedlines=%-4s  -> logs/30_%s.log\n' "$name" "$rc" "$p" "$f" "$name"
  return 0
}

echo "=== G2 thermodynamic closure ==="
run g2_eos           tests/unit/compressible/CFDCompressibleTests 'IdealGasEOSTest*:ThermodynamicPropertiesTest*:MachNumberTest*'
run g2_totalenergy   tests/unit/compressible/CFDCompressibleTests 'TotalEnergy*:SensibleEnthalpyTest*'

echo "=== G3 variable density / continuity ==="
run g3_continuity    tests/unit/compressible/CFDCompressibleTests 'CompressibleContinuityTest*:CompressibleMassFluxTest*'
run g3_density       tests/unit/physics/CFDPhysicsTests 'EvaluateDensityFieldTest*:DensityPropertyDemonstrationTest*:TimeDerivativeFieldDensity*'

echo "=== G4 momentum + energy conservation ==="
run g4_momentum      tests/unit/compressible/CFDCompressibleTests 'CompressibleMomentum*:CompressibleRelaxedMomentumTest*:DilatationalStressTest*'
run g4_dbconserve    tests/unit/compressible/CFDCompressibleTests 'DensityBasedConservationTest*:DensityBasedStateTest*'

echo "=== G5 low Mach ==="
run g5_lowmach       tests/integration/compressible/CFDLowMachFamilyTests '*'

echo "=== G6 HLLC ==="
run g6_hllc          tests/unit/compressible/CFDCompressibleTests 'Hllc*'

echo "=== G7 shock validation ==="
run g7_shock         tests/integration/compressible/CFDShockValidationTests '*'

echo "=== G8 CPU/GPU equivalence ==="
run g8_gpu           tests/unit/gpu/CFDGpuTests 'DensityBasedGpuEquivalence*:HllcGpuEquivalenceTest*'
run g8_thermo        tests/unit/compressible/CFDCompressibleTests 'DeviceThermodynamicsEquivalence*'

echo "=== G9 determinism ==="
run g9_determinism   tests/unit/compressible/CFDCompressibleTests '*Determinism*'

echo "=== P17.8 canonical ==="
run canonical        tests/integration/compressible/CFDDensityBasedCanonicalTests '*'

echo "=== P17.10 dry-run + the recorded finding ==="
run p17_10_dryrun    tests/integration/compressible/CFDP17_10DryRunTests '*'

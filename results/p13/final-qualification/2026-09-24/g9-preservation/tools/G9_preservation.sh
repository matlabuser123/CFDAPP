#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G9, P14/P15/P16 preservation.
#
# BUILD TREE SUBSTITUTION, recorded rather than silent.
# The frozen P14/P15/P16 scripts hardcode `build/final`, which was Release+CUDA
# when they were written. It is now CFDAPP_ENABLE_CUDA:BOOL=OFF and has no
# cuda/libcfdcuda.a, so P14's fingerprint.sh would not even link and every GPU
# suite would be absent. G9 therefore uses `build/qual-final` -- the G1
# qualification tree, Release + CUDA ON, libcfdcuda.a present. Same criteria,
# same tests, a tree that can actually run them. This is the third gate in which
# build/final turned out not to be what a frozen script assumed (G6: stale
# binary, G8: CUDA OFF).
#
# The three frozen `final.sh` scripts are NOT run whole: each is a
# three-configuration full regression, which is G10's gate.
#
# DOCUMENTED EXCLUSIONS, copied verbatim from
# results/p16/turbulence/tools/regress_both.sh and not widened:
#   SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|
#   WallArchitectureDiagnostic          (the four deferred automatic-wall suites)
#   DriverSeegmiller.CorrectedMeshSequence   (preserved failing P20 benchmark)
# They are excluded from the GREEN suites and then run SEPARATELY, where RED is
# the required outcome.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G9=$ROOT/results/p13/final-qualification/2026-09-24/g9-preservation
L=$G9/logs
B=build/qual-final
cd "$ROOT"
mkdir -p "$L"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller\.CorrectedMeshSequence'

log=$L/G9_preservation.log
{
echo "P13 G9 -- P14/P15/P16 preservation"
echo "date: $(date -u +%FT%TZ)"
echo "tree: $B"
grep -E 'CFDAPP_ENABLE_CUDA|CMAKE_BUILD_TYPE' "$B/CMakeCache.txt" | sed 's/^/  /'
echo "  libcfdcuda.a: $([ -f $B/cuda/libcfdcuda.a ] && echo present || echo ABSENT)"
echo ""

# ---------------------------------------------------------------- helper -----
suite() {  # suite <label> <ctest -R regex>
  local label="$1" rx="$2"
  ctest --test-dir "$B" -R "$rx" -E "$EXCLUDE" --timeout 7200 --output-on-failure \
    > "$L/G9_$label.log" 2>&1
  local rc=$?
  local n p f
  n=$(grep -aoE 'out of [0-9]+' "$L/G9_$label.log" | tail -1 | grep -oE '[0-9]+')
  p=$(grep -aoE '[0-9]+% tests passed' "$L/G9_$label.log" | tail -1)
  f=$(grep -acE '\(Failed\)|\(Timeout\)' "$L/G9_$label.log" || true)
  printf '  %-26s rc=%-3s %-20s tests=%-5s failed=%s\n' "$label" "$rc" "${p:-n/a}" "${n:-0}" "$f"
  if [ "$rc" -ne 0 ]; then
    echo "    --- non-passing ---"
    grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)' "$L/G9_$label.log" | sed 's/^/      /' | head -12
  fi
}

echo "### Step 2 -- P14 generic transport ###"
suite p14_transport     'Transport|LegacyMatrix|ScalarTransport'
suite p14_gpu_transport 'DeviceScalarTransportSemantics|DeviceMaxAbsDifference|DeviceField|DeviceBuffer'
echo ""

echo "### Step 2 -- the P13-001 SIMPLE fingerprint, bitwise ###"
echo "  (P14's sharpest preservation instrument: a passing CTest does not prove"
echo "   SIMPLE produced the same BITS; this does.)"
g++ -std=c++20 -O2 -DNDEBUG -I include -I "$B/generated/include" \
    -I /usr/local/cuda-12.9/include results/p13/restart/tools/solve_fingerprint.cpp \
    -o /tmp/g9_fingerprint -Wl,--start-group "$B/src/libcfdcore.a" \
    "$B/cuda/libcfdcuda.a" -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$L/G9_fp_build.log"
if [ $? -ne 0 ]; then
  echo "  COMPILE FAILED -- see G9_fp_build.log"; tail -5 "$L/G9_fp_build.log" | sed 's/^/    /'
else
  /tmp/g9_fingerprint > "$L/G9_fingerprint.log" 2>&1
  echo "  cases fingerprinted: $(grep -c FINGERPRINT "$L/G9_fingerprint.log")"
  BASE=results/p13/restart/logs/fingerprint-after-histories.log
  strip() { grep -v '^#' "$1" | grep -v '^$'; }
  if diff <(strip "$BASE") <(strip "$L/G9_fingerprint.log") > "$L/G9_fingerprint_diff.txt" 2>&1; then
    echo "  vs the P13-001 frozen baseline: IDENTICAL -- SIMPLE's bits have not moved"
  else
    echo "  *** DIFFERS from the P13-001 baseline ***"
    head -20 "$L/G9_fingerprint_diff.txt" | sed 's/^/    /'
  fi
fi
echo ""

echo "### Step 3 -- P15 thermal / energy ###"
suite p15_energy      'EnergyEquation|Enthalpy|ThermalPropert|TemperatureDependent|ConstantProperty|FluidProperties'
suite p15_thermal_bc  'FixedTemperature|HeatFlux|ConvectiveBoundary|Adiabatic|Conjugate|InterfaceConductance'
suite p15_validation  'ThermalValidation|HeatedCavity|NaturalConvection|Boussinesq|TurbulentConductivity|VariablePropert'
echo ""

echo "### Step 4 -- P16 turbulence (adopted scope, deferred suites excluded) ###"
suite p16_unit        'Turbulence|EffectiveViscosity|WallFunction|WallTreatment|Spalding|MomentumWallFlux|KEpsilon|KOmega'
suite p16_channel     'ChannelFlowValidation|TurbulentChannel|ChannelBoundary'
suite p16_thermal     'TurbulentThermal|TurbulentHeatTransfer'
echo ""

echo "### Step 5 -- P16 GPU equivalence ###"
suite p16_gpu_turb    'DeviceTurbulenceAlgebra|DeviceKTransport|DeviceEpsilonTransport|DeviceKEpsilonCorrection|DeviceEffectiveViscosity|DeviceWallFaceViscosity|BoundaryEncoding'
suite p16_gpu_resident 'Resident|TurbulenceBackendTrace|ProductionKEpsilonBackend|GpuKEpsilon'
suite p16_gpu_determinism 'GpuBiCGSTABRestart|KrylovRestartParity|GpuResidencyManager|GpuBackendCpuFallback'
echo ""

echo "### Step 4/6 -- the PRESERVED P20 failures: RED is the required outcome ###"
CHAN="$B/tests/integration/turbulence_channel/CFDTurbulenceChannelValidationTests"
if [ -x "$CHAN" ]; then
  "$CHAN" --gtest_filter='SSTAutomaticWallChannel.*:SSTCompositeWallChannel.*:SSTMomentumFluxChannel.*:WallArchitectureDiagnostic.*' \
    > "$L/G9_p20_known_failures.log" 2>&1
  echo "  exit status: $?  (non-zero is REQUIRED -- these are deferred to P20)"
  grep -aE '^\[  (PASSED|FAILED)  \]|tests? from .* ran' "$L/G9_p20_known_failures.log" | tail -6 | sed 's/^/    /'
  echo "  individually:"
  grep -aE '^\[  FAILED  \] [A-Z]' "$L/G9_p20_known_failures.log" | sed 's/^/      /' | head -10
else
  echo "  channel binary MISSING at $CHAN"
fi
echo ""

echo "### Step 6 -- the documented exclusions, verified unchanged ###"
echo "  filter used by this gate, copied from regress_both.sh:"
echo "    $EXCLUDE"
echo "  regress_both.sh still declares the same set:"
grep -nE "^EXCLUDE=|^DEFERRED_BFS=" results/p16/turbulence/tools/regress_both.sh | sed 's/^/    /'
echo "  P16 scope statement in TODO.md:"
grep -nE 'k-omega SST|P20' TODO.md | head -4 | sed 's/^/    /'
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"

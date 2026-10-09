#!/usr/bin/env bash
# P17.11 section 5 -- the focused P17 suite, run on the final candidate BEFORE the long regressions.
#
# The filter is written out explicitly and its suite list is printed, so "N tests ran" is
# attributable to named suites rather than to a regex nobody checked. It deliberately INCLUDES a few
# P15-era suites (EnergyEquation*, SensibleEnthalpy) because P17.5 builds directly on them and a
# break there is a P17 problem too; running extra tests costs seconds and hides nothing.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
Q=results/p17/qualification
TREE="${1:-build/qual-final}"
LABEL="${2:-release_cuda}"

P17RE='IdealGasEOSTest|ThermodynamicPropertiesTest|MachNumberTest|EvaluateDensityFieldTest'
P17RE="$P17RE"'|DensityPropertyDemonstrationTest|TimeDerivativeFieldDensity'
P17RE="$P17RE"'|CompressibleContinuityTest|CompressibleMassFluxTest'
P17RE="$P17RE"'|CompressibleMomentum|CompressibleRelaxedMomentumTest|DilatationalStressTest'
P17RE="$P17RE"'|TotalEnergy|SensibleEnthalpyTest|EnergyEquation'
P17RE="$P17RE"'|LowMachFamilyTest|LowMachRegressionTest'
P17RE="$P17RE"'|Hllc'
P17RE="$P17RE"'|DensityBased'
P17RE="$P17RE"'|SodShockTubeTest|NormalShockTest|ContactDiscontinuityTest|CompressionExpansionTest'
P17RE="$P17RE"'|ShockConservationTest|ShockDetectorTest|ShockDeterminismTest|SupersonicAdvection'
P17RE="$P17RE"'|P17_10_DryRun|P17_10_Finding|DeviceThermodynamicsEquivalence'

echo "=== focused P17 suite -- $LABEL ($TREE) ==="
echo "filter: $P17RE"
echo ""
echo "--- suites matched, by name ---"
ctest --test-dir "$TREE" -N -R "$P17RE" 2>/dev/null \
  | grep -oE 'Test +#[0-9]+: [A-Za-z0-9_]+' | sed -E 's/.*: //' | sort | uniq -c | sort -rn | sed 's/^/  /'
REG=$(ctest --test-dir "$TREE" -N -R "$P17RE" 2>/dev/null | grep -oE 'Total Tests: [0-9]+' | grep -oE '[0-9]+')
echo ""
echo "registered by the filter : ${REG:-0}"
if [ "${REG:-0}" -lt 100 ]; then
  echo "*** FEWER THAN 100 TESTS MATCHED -- the filter is probably wrong. FAILING CLOSED. ***"
  exit 1
fi

echo ""
echo "--- running ---"
t0=$(date +%s)
OMP_NUM_THREADS=1 ctest --test-dir "$TREE" -j 8 --timeout 7200 -R "$P17RE" --output-on-failure \
  > "$Q/logs/20_focused_${LABEL}.log" 2>&1
rc=$?
secs=$(( $(date +%s) - t0 ))
echo "ctest exit  : $rc"
echo "runtime     : ${secs} s"
grep -aoE '[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "$Q/logs/20_focused_${LABEL}.log" | tail -1 | sed 's/^/summary     : /'
echo "passed      : $(grep -acE '   Passed' "$Q/logs/20_focused_${LABEL}.log")"
echo "failed      : $(grep -acE '\*\*\*Failed|\*\*\*Timeout|\*\*\*Exception' "$Q/logs/20_focused_${LABEL}.log")"
echo "skipped     : $(grep -acE '\*\*\*Skipped' "$Q/logs/20_focused_${LABEL}.log")"
echo "disabled    : $(grep -acE '\*\*\*Not Run \(Disabled\)' "$Q/logs/20_focused_${LABEL}.log")"
if [ "$rc" -ne 0 ]; then
  echo "--- every non-passing test ---"
  grep -aE '\(Failed\)|\(Timeout\)|\(Not Run\)|\(Skipped\)' "$Q/logs/20_focused_${LABEL}.log" | sed 's/^/    /'
fi
exit "$rc"

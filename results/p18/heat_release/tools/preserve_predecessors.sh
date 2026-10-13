#!/usr/bin/env bash
# P18.6 sections 53-58 -- PREDECESSOR PRESERVATION.
#
# Runs each predecessor phase's own suites and records the exact counts.
#
# THE PROBE FAILS CLOSED ON A ZERO-MATCH FILTER. In P18.3 a preservation filter named
# `*TemperatureDependent*` while the suite was `TemperatureProperty`: it matched 0 tests and
# reported a pass. A 0-test filter reporting "all passed" is a vacuous claim, so every filter
# below is counted BEFORE it is run and a 0 count is a hard failure of this probe.
#
# The suite -> phase map was taken from the test sources, not assumed:
#   P18.1  MassFractionStateTest SpeciesRegistryTest                 test_mass_fraction_state.cpp
#   P18.2  Species{Boundary,Canonical,Closure,Conservation,Control,
#          FaceConservation,Source,Transient,Transport}Test          test_species_transport.cpp
#   P18.3  MixtureThermodynamicsTest SpeciesThermodynamicsTest       test_mixture_thermodynamics.cpp
#   P18.4  Reaction{,Mechanism,Source,Transport}Test                 test_reaction_mechanism.cpp
#   P18.5  ArrheniusTest ArrheniusKineticsTest                       test_arrhenius_kinetics.cpp
#   P18.6  FormationEnthalpy/ReactionEnthalpy/HeatSource/Energy
#          Accounting/ClosedReactor/TemperatureFeedback/
#          NonReactingLimit/HeatReleaseDeterminism                   test_heat_release.cpp
#   pre-P18 species (P14/P15): the four remaining files. P18.5 changed no production file they
#          touch, and they are run anyway because that is the claim being checked, not assumed.
set -u

cd "$(dirname "$0")/../../../.." || exit 1
BIN=build/final/tests/unit/species/CFDSpeciesTests
if [[ ! -x "$BIN" ]]; then
  echo "FAIL: $BIN is missing or not executable"
  exit 1
fi

echo "=== P18.6 PREDECESSOR PRESERVATION ==="
echo "binary : $BIN"
echo "sha256 : $(sha256sum "$BIN" | cut -d' ' -f1)"
echo "date   : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo

status=0

run_group() {
  local label="$1"
  local filter="$2"

  # COUNT FIRST. A filter that matches nothing cannot preserve anything.
  local listed
  listed=$("$BIN" --gtest_list_tests --gtest_filter="$filter" 2>/dev/null |
    grep -cE "^  [A-Za-z]")
  if [[ "$listed" -eq 0 ]]; then
    echo "[$label] VACUOUS FILTER -- matched 0 tests: $filter"
    echo "         This probe fails closed rather than reporting a pass over nothing."
    status=1
    return
  fi

  local out
  out=$("$BIN" --gtest_filter="$filter" 2>&1)
  local ran passed failed
  ran=$(echo "$out" | sed -n 's/^\[==========\] \([0-9]*\) tests\? from .*ran.*/\1/p' | tail -1)
  passed=$(echo "$out" | sed -n 's/^\[  PASSED  \] \([0-9]*\) tests\?.*/\1/p' | tail -1)
  failed=$(echo "$out" | sed -n 's/^\[  FAILED  \] \([0-9]*\) tests\?,.*/\1/p' | tail -1)
  [[ -z "$failed" ]] && failed=0

  if [[ "$listed" != "$ran" ]]; then
    echo "[$label] FAIL: listed $listed but ran $ran"
    status=1
    return
  fi
  if [[ "$failed" != "0" || "$passed" != "$ran" ]]; then
    echo "[$label] FAIL: ran=$ran passed=$passed failed=$failed"
    echo "$out" | grep -E "^\[  FAILED  \]" | sed 's/^/         /'
    status=1
    return
  fi
  printf "[%-8s] listed=%-4s ran=%-4s passed=%-4s failed=%s\n" \
    "$label" "$listed" "$ran" "$passed" "$failed"
}

run_group "P18.1" "MassFractionStateTest.*:SpeciesRegistryTest.*"
run_group "P18.2" "SpeciesBoundaryTest.*:SpeciesCanonicalTest.*:SpeciesClosureTest.*:SpeciesConservationTest.*:SpeciesControlTest.*:SpeciesFaceConservationTest.*:SpeciesSourceTest.*:SpeciesTransientTest.*:SpeciesTransportTest.*"
run_group "P18.3" "MixtureThermodynamicsTest.*:SpeciesThermodynamicsTest.*"
run_group "P18.4" "ReactionTest.*:ReactionMechanismTest.*:ReactionSourceTest.*:ReactionTransportTest.*"
run_group "P18.5" "ArrheniusTest.*:ArrheniusKineticsTest.*"
run_group "P18.6" "FormationEnthalpyTest.*:ReactionEnthalpyTest.*:HeatSourceTest.*:EnergyAccountingTest.*:ClosedReactorTest.*:TemperatureFeedbackTest.*:NonReactingLimitTest.*:HeatReleaseDeterminismTest.*"
run_group "pre-P18" "ConcentrationBoundsTest.*:SpeciesBoundaryConditionTest.*:SpeciesEquationAssemblyTest.*:SpeciesEquationConvectionTest.*:SpeciesEquationDiffusionTest.*:SpeciesEquationSourceTest.*:SpeciesNonOrthogonalTest.*:SpeciesPropertiesTest.*:SpeciesSolverTest.*"

echo
echo "--- whole binary, for the arithmetic cross-check ---"
"$BIN" 2>&1 | tail -3

echo
if [[ "$status" -eq 0 ]]; then
  echo "PRESERVATION: PASS -- every group matched a non-zero set and every test in it passed"
else
  echo "PRESERVATION: FAIL"
fi
exit "$status"

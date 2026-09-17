#!/usr/bin/env bash
# P12-DIFF-002 A4-2: capture a one-line reason for every failing test, so the inventory's
# classification rests on the actual assertion that failed rather than on the test's name.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
LOG=$R/results/p12-diff-002/a4/logs/02_failure_reasons.log
cd $R

run() {  # run <binary> <gtest filter>
  local bin=$1 filter=$2
  echo "### $filter"
  "$bin" --gtest_filter="$filter" 2>&1 |
    grep -E 'Failure$|actual:|Which is:|bound|error|observed order|: max |vs ' |
    grep -vE '^\[' | head -6 | sed 's/^/    /'
  echo
}

{
  echo "# P12-DIFF-002 A4-2 failure reasons; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# library $(sha256sum $B/src/libcfdcore.a | cut -c1-16)"
  echo
  D=$B/tests/unit/discretization/CFDDiscretizationTests
  P=$B/tests/unit/physics/CFDPhysicsTests
  T=$B/tests/unit/thermal/CFDThermalTests
  S=$B/tests/unit/species/CFDSpeciesTests
  run "$D" 'BoundaryReconstruction.AssembledThermalSystemMatchesTheHandDerivedOne'
  for f in MomentumDiffusionTest.InternalFaceCoefficientsAreSymmetric \
           MomentumVariableViscosityTest.InternalFaceMatchesHandDerivedLinearMuValue \
           MomentumVariableViscosityTest.TabulatedMuGivesTheExpectedFaceValue; do
    run "$P" "$f"
  done
  for f in EnergyEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients \
           EnergyEquationDiffusionTest.InternalFaceCoefficientsAreSymmetric \
           EnergyEquationAssemblyTest.CombinedAssemblyMatchesHandDerivedCoefficients \
           EnergyEquationThermalBCIntegrationTest.FixedTemperatureGivesIdenticalResultToEquivalentFixedValue \
           EnergyEquationVariablePropertiesTest.DiffusionInternalFaceMatchesHandDerivedValue \
           EnergyEquationVariablePropertiesTest.DiffusionBoundaryFaceUsesOwnerConductivityDirectly \
           RegionAwareThermalDiffusionTest.EqualConductivityMatchesSingleMaterialPathExactly \
           ThermalBoundaryConsistency.ConvergedFieldIsConsistentWithItsBoundaryValues \
           ThermalBoundaryConsistency.ConjugateConductionPathIsConsistentToo \
           SparseAssembly3DTest.TwoCellsInX SparseAssembly3DTest.TwoByTwoByOne \
           SparseAssembly3DTest.TwoByTwoByTwo \
           SparseAssembly3DTest.TwoCellsWithUpwindConvectionAndAZeroGradientOutlet; do
    run "$T" "$f"
  done
  for f in SpeciesEquationDiffusionTest.TwoCellSystemMatchesHandDerivedCoefficients \
           SpeciesEquationDiffusionTest.InternalFaceCoefficientsAreSymmetric \
           SpeciesEquationAssemblyTest.CombinedAssemblyUsesDensityTimesDiffusivityAsCoefficient; do
    run "$S" "$f"
  done
  run "$B/tests/solver/simple/CFDSimpleTests" 'SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline'
  run "$B/tests/solver/simple/CFDSimpleTests" 'SIMPLERobustnessTest.DivergenceStatus'
  run "$B/tests/integration/poiseuille/CFDPoiseuilleValidationTests" 'PoiseuilleValidation.ProductionGridConvergence'
  run "$B/tests/integration/thermal/CFDNaturalConvectionValidationTests" 'NaturalConvectionValidation.Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3'
  run "$B/tests/integration/species/CFDSpeciesConservationValidationTests" 'SpeciesConservationTest.OpenChannelWithVolumetricSourceBalancesNetOutflowAgainstSource'
  run "$B/tests/integration/lowmach/CFDLowMachRegressionTests" 'LowMachRegressionTest.GlobalMassImbalanceIsSmall'
  run "$B/tests/integration/mms/CFDMMSValidationTests" 'SIMPLEMMS.VelocityConvergesAtExpectedOrder'
  run "$B/tests/integration/mms/CFDMMSValidationTests" 'SIMPLEMMS.DistortedMesh'
} > $LOG 2>&1
cat $LOG

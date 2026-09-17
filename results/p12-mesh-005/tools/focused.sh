#!/usr/bin/env bash
# P12-MESH-005 focused tests in the authorized stage order (Release -O3), stopping at the first failure.
# Each stage: binary, gtest filter; exact counts from gtest's own summary lines.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release/tests
cd $R
TOTAL_RUN=0; TOTAL_PASS=0; TOTAL_FAIL=0
stage() { # number name binary filter [extra-args]
  local out; out=$($B/$3 --gtest_filter="$4" $5 2>&1)
  local ran; ran=$(echo "$out" | grep -E "^\[==========\] [0-9]+ tests? from .* ran" | sed -E 's/^\[==========\] ([0-9]+) tests?.*/\1/')
  local passed; passed=$(echo "$out" | grep -E "^\[  PASSED  \] [0-9]+" | sed -E 's/^\[  PASSED  \] ([0-9]+).*/\1/')
  local failed; failed=$(echo "$out" | grep -E "^\[  FAILED  \] [0-9]+ tests?," | sed -E 's/^\[  FAILED  \] ([0-9]+).*/\1/')
  ran=${ran:-0}; passed=${passed:-0}; failed=${failed:-0}
  printf "%-3s %-22s %-40s %-70s ran %4d passed %4d failed %d\n" "$1" "$2" "$(basename $3)" "$4" "$ran" "$passed" "$failed"
  TOTAL_RUN=$((TOTAL_RUN+ran)); TOTAL_PASS=$((TOTAL_PASS+passed)); TOTAL_FAIL=$((TOTAL_FAIL+failed))
  if [ "$failed" != "0" ] || [ "$ran" = "0" ] || [ "$ran" != "$passed" ]; then
    echo "$out" | grep -E "Failure|FAILED|error" | head -40
    echo "STOP: stage $1 ($2) failed"
    echo "TOTAL (stopped): ran $TOTAL_RUN, passed $TOTAL_PASS, failed $TOTAL_FAIL"
    exit 1
  fi
}
echo "# P12-MESH-005 staged focused tests, Release; $(date -u)"
M=unit/mesh/CFDMeshTests
stage 1  geometry           unit/core/CFDCoreTests 'Vector3Test.*'
stage 1b geometry           $M 'Cartesian3DMeshTest.TopologyCountsMatchIndependentFormulas:Cartesian3DMeshTest.BuildIsDeterministicAndFollowsTheDocumentedNumbering:Cartesian3DMeshTest.DimensionIsThreeFor3DMeshesAndTwoForEvery2DBuilder:Cartesian3DMeshTest.StructuredGridMustMatchTheMeshDimension:Cartesian3DMeshTest.InvalidDimensionsAndExtentsAreRejected:Cartesian3DMeshTest.FingerprintSeesZIn3DAndKeeps2DValues:Cartesian3DMeshTest.RequireTwoDimensionalNamesTheComponent'
stage 2  volume             $M 'Cartesian3DMeshTest.CellVolumesAreBoxVolumesAndSumToTheDomain'
stage 3  area-vector        $M 'Cartesian3DMeshTest.AreaVectorsHaveTheRightMagnitudeDirectionAndClosure'
stage 4  centroid           $M 'Cartesian3DMeshTest.CentroidsAreBoxAndRectangleCentres'
stage 5  connectivity       $M 'Cartesian3DMeshTest.EachCellHasSixFacesInCanonicalOrderAndEachFaceItsOwners'
stage 6  boundary-patch     $M 'Cartesian3DMeshTest.SixPatchesPartitionTheBoundaryWithOutwardNormals'
stage 7  scalar-field       unit/fields/CFDFieldTests 'Fields3DTest.ScalarFieldsSampleLinearFieldsWithExactVolumeMeans'
stage 8  vector-field       unit/fields/CFDFieldTests 'Fields3DTest.VectorFieldsCarryThreeComponents'
D=unit/discretization/CFDDiscretizationTests
stage 9  interpolation      $D 'Operators3DTest.InterpolationIsExactForConstantAndLinearFields'
stage 10 gradient           $D 'Operators3DTest.GradientsAreExactForLinearFieldsInEveryCell:Operators3DTest.LeastSquaresPrimitiveSolvesTheThreeByThreeSystem'
stage 11 diffusion          $D 'Operators3DTest.ExplicitDiffusion*'
stage 12 convection         $D 'Operators3DTest.DivergenceFreeMassFluxBalancesInEveryCell:Operators3DTest.ConvectionPreservesAConstantWithEveryScheme:Operators3DTest.HigherOrderConvectionIsExactForALinearFieldAwayFromTheBoundary'
stage 12b 2D-only-guards    $D 'Operators3DTest.TwoDimensionalOnlyComponentsRefuseA3DMesh'
stage 13 sparse-assembly    unit/thermal/CFDThermalTests 'SparseAssembly3DTest.*'
stage 14 VTK                unit/io/CFDIoTests 'VTK3DTest.*'
stage 15 MMS                integration/mms/CFDMMSValidationTests 'MMS3DTest.ForcingMatchesFiniteDifferencesOfClosedForms:MMS3DTest.OperatorErrorsDecreaseOnCoarseLevels'
stage 16 convergence        integration/mms/CFDMMSValidationTests 'MMS3DTest.DISABLED_OperatorRefinementStudy' --gtest_also_run_disabled_tests
# 17 -- two-dimensional backward compatibility: the MESH-001..004 suites by name, then whole suites.
C=integration/case/CFDCaseIntegrationTests
stage 17a MESH-001-distorted $M 'StructuredQuadMesh.*:MeshValidity.*'
stage 17b MESH-001-distorted unit/io/CFDIoTests 'StructuredQuadCase.*'
stage 17c MESH-001-distorted $D 'ObliqueNeumannGradient.*'
stage 17d MESH-001-distorted $C 'StructuredQuadProductionCase.*'
stage 17e MESH-002-graded   $M 'MeshGrading.*:AxisSpacing.*:GradedMesh.*'
stage 17f MESH-002-graded   unit/io/CFDIoTests 'GradedMeshCase.*'
stage 17g MESH-002-graded   $C 'GradedMeshProductionCase.*'
stage 17h MESH-003-multiblk $M 'MultiBlockMesh.*:MultiBlockMeshInvalid.*'
stage 17i MESH-003-multiblk unit/io/CFDIoTests 'MultiBlockCase.*'
stage 17j MESH-003-multiblk $C 'MultiBlockProductionCase.*'
stage 17k MESH-004-quality  $M 'MeshQuality.*:MeshQualityReport.*'
stage 17l MESH-004-quality  unit/io/CFDIoTests 'MeshQualityCase.*:JSONWriterTest.*'
stage 17m MESH-004-quality  $C 'MeshQualityCampaign.*'
stage 17n MESH-004-MMS      integration/mms/CFDMMSValidationTests '*'
stage 17o whole-suites      $M '*'
stage 17p whole-suites      unit/io/CFDIoTests '*'
stage 17q whole-suites      $D '*'
stage 17r whole-suites      unit/thermal/CFDThermalTests '*'
stage 17s whole-suites      unit/fields/CFDFieldTests '*'
stage 17t whole-suites      unit/core/CFDCoreTests '*'
stage 17u whole-suites      unit/validation/CFDValidationUnitTests '*'
echo "TOTAL: ran $TOTAL_RUN, passed $TOTAL_PASS, failed $TOTAL_FAIL (stages overlap: a test can be counted in two stages)"

#!/usr/bin/env bash
# P12-MESH-006 focused tests on the final binaries (after A3, GUI, CLI fixtures, clang-format), in the
# authorized order, stopping at the first failed stage. gtest stages run in Release (-O3); the GUI stage
# in Debug (GUI on, Qt offscreen); the CLI stage through ctest. Exact counts from gtest's/ctest's own
# summary lines (stages may overlap: one test can be counted in two stages).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release/tests
export QT_QPA_PLATFORM=offscreen  # the GUI controller tests (no display)
cd $R
TOTAL_RUN=0; TOTAL_PASS=0; TOTAL_FAIL=0; TOTAL_DISABLED=0
stage() { # number name binary filter [extra-args]
  local out; out=$($3 --gtest_filter="$4" $5 2>&1)
  local ran; ran=$(echo "$out" | grep -E "^\[==========\] [0-9]+ tests? from .* ran" | sed -E 's/^\[==========\] ([0-9]+) tests?.*/\1/')
  local passed; passed=$(echo "$out" | grep -E "^\[  PASSED  \] [0-9]+" | sed -E 's/^\[  PASSED  \] ([0-9]+).*/\1/')
  local failed; failed=$(echo "$out" | grep -E "^\[  FAILED  \] [0-9]+ tests?," | sed -E 's/^\[  FAILED  \] ([0-9]+).*/\1/')
  local disabled; disabled=$(echo "$out" | grep -E "YOU HAVE [0-9]+ DISABLED" | sed -E 's/.*YOU HAVE ([0-9]+).*/\1/')
  ran=${ran:-0}; passed=${passed:-0}; failed=${failed:-0}; disabled=${disabled:-0}
  printf "%-4s %-24s %-32s %-72s ran %4d passed %4d failed %d disabled %d\n" "$1" "$2" "$(basename $3)" "$4" "$ran" "$passed" "$failed" "$disabled"
  TOTAL_RUN=$((TOTAL_RUN+ran)); TOTAL_PASS=$((TOTAL_PASS+passed)); TOTAL_FAIL=$((TOTAL_FAIL+failed)); TOTAL_DISABLED=$((TOTAL_DISABLED+disabled))
  if [ "$failed" != "0" ] || [ "$ran" = "0" ] || [ "$ran" != "$passed" ]; then
    echo "$out" | grep -E "Failure|FAILED|error" | head -40
    echo "STOP: stage $1 ($2) failed"
    echo "TOTAL (stopped): ran $TOTAL_RUN, passed $TOTAL_PASS, failed $TOTAL_FAIL"
    exit 1
  fi
}
echo "# P12-MESH-006 staged focused tests (final binaries), $(date -u +%Y-%m-%dT%H:%M:%SZ)"
M=$B/unit/mesh/CFDMeshTests; D=$B/unit/discretization/CFDDiscretizationTests; IO=$B/unit/io/CFDIoTests
S=$B/solver/simple/CFDSimpleTests; MMS=$B/integration/mms/CFDMMSValidationTests
C=$B/integration/case/CFDCaseIntegrationTests; APP=$B/unit/app/CFDAppLayerTests
# --- 3D (MESH-006, with the MESH-005 foundation it builds on) ---
stage 1  3D-mesh             $M 'Cartesian3DMeshTest.*'
stage 2  3D-case-parsing     $IO 'Case3DTest.*'
stage 3  2D-only-guards      $D 'Operators3DTest.*'
stage 4  w-momentum          $S 'SIMPLE3D.MomentumSystemsArePermutationSymmetric:SIMPLE3D.MonitorNeverConvergesWhileWIsUnconverged'
stage 5  3D-face-flux/RC     $S 'SIMPLE3D.UniformFlowFluxIsExactOnEveryFaceOrientation:SIMPLE3D.RhieChowVanishesForALinearPressureField:SIMPLE3D.RhieChowSeesTheCheckerboardTheLinearFluxCannot'
stage 6  3D-SIMPLE           $S 'SIMPLE3D.*'
stage 7  MMS                 $MMS 'MMSSimple3DTest.*:MMS3DTest.*'
stage 8  duct/cavity/export  $C 'Duct3DProductionCase.*:LidDrivenCube3DProductionCase.*'
stage 9  VTK/JSON/CSV        $IO 'VTK3DTest.*:JSONWriterTest.*:CSVWriterTest.*'
stage 10 app-layer           $APP '*'
# --- G10.3: the MESH-001..005 suites by name, then whole suites ---
stage 11a MESH-001-distorted $M 'StructuredQuadMesh.*:MeshValidity.*'
stage 11b MESH-001-distorted $IO 'StructuredQuadCase.*'
stage 11c MESH-001-distorted $D 'ObliqueNeumannGradient.*'
stage 11d MESH-001-distorted $C 'StructuredQuadProductionCase.*'
stage 11e MESH-002-graded    $M 'MeshGrading.*:AxisSpacing.*:GradedMesh.*'
stage 11f MESH-002-graded    $IO 'GradedMeshCase.*'
stage 11g MESH-002-graded    $C 'GradedMeshProductionCase.*'
stage 11h MESH-003-multiblk  $M 'MultiBlockMesh.*:MultiBlockMeshInvalid.*'
stage 11i MESH-003-multiblk  $IO 'MultiBlockCase.*'
stage 11j MESH-003-multiblk  $C 'MultiBlockProductionCase.*'
stage 11k MESH-004-quality   $M 'MeshQuality.*:MeshQualityReport.*'
stage 11l MESH-004-quality   $IO 'MeshQualityCase.*'
stage 11m MESH-004-quality   $C 'MeshQualityCampaign.*'
stage 11n MESH-005-3D        $B/unit/fields/CFDFieldTests 'Fields3DTest.*'
stage 11o MESH-005-3D        $B/unit/thermal/CFDThermalTests 'SparseAssembly3DTest.*'
stage 12a whole-suites       $M '*'
stage 12b whole-suites       $IO '*'
stage 12c whole-suites       $D '*'
stage 12d whole-suites       $S '*'
stage 12e whole-suites       $MMS '*'
stage 12f whole-suites       $C '*'
stage 12g whole-suites       $B/unit/core/CFDCoreTests '*'
stage 12h whole-suites       $B/unit/fields/CFDFieldTests '*'
stage 12i whole-suites       $B/unit/validation/CFDValidationUnitTests '*'
# --- GUI (Debug, GUI on) ---
stage 13 GUI                 $R/build/debug/apps/gui/CFDGuiControllerTests '*'
# --- CLI (ctest: every cfdapp process test, 2D and 3D) ---
out=$(cd build/release && ctest -R "CFDAppCli|CFDAppSmokeTest" 2>&1)
echo "$out" | grep -E "tests passed|Test +#" | sed 's/^/     /'
cli=$(echo "$out" | grep -E "tests passed")
echo "14   CLI (ctest -R CFDAppCli|CFDAppSmokeTest): $cli"
echo "$cli" | grep -q "100% tests passed" || { echo "STOP: CLI stage failed"; exit 1; }
echo "TOTAL gtest stages: ran $TOTAL_RUN, passed $TOTAL_PASS, failed $TOTAL_FAIL (stages overlap); CLI: $cli"

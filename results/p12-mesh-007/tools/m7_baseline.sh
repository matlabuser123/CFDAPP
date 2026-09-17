#!/usr/bin/env bash
# P12-MESH-007 baseline, before any MESH-007 source change (evidence only):
#   1. git state (HEAD, status, diff stat);
#   2. MESH-006 stability: build/release rebuilt (no source change expected since the MESH-006 closeout:
#      its cfdapp must still be byte-identical to MESH-006's final binary, sha256 df4ee6d0...), then the
#      MESH-006 3D suites, the 2D/3D CLI process tests and the existing PISO/transient suites;
#   3. the known pre-existing sanitizer defect (MeshQualityReport.DisconnectedMeshIsFatal, P12-MESH-004
#      test code) reproduced in build/asan with CI's ASan/UBSan options -- recorded, NOT fixed;
#   4. the pre-MESH-007 reference tree: every tracked and untracked-not-ignored file copied to
#      $HOME/m7ref/base, src/include hashed, built in Release with build/release's options (dependency
#      sources from build/release/_deps, no network).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-007/logs
B=$HOME/m7ref/base
mkdir -p $L
cd $R
{
  echo "# P12-MESH-007 baseline (before any MESH-007 change); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "\$ git rev-parse HEAD"; git rev-parse HEAD
  echo "\$ git log -1 --oneline"; git log -1 --oneline
  echo "\$ git status --short | wc -l: $(git status --short | wc -l) ($(git status --short | grep -c '^ M') modified, $(git status --short | grep -c '^??') untracked)"
  echo "\$ git diff --stat | tail -1"; git diff --stat | tail -1
  echo "\$ git status --short"; git status --short
} > $L/00_baseline_git.log 2>&1
tail -n +2 $L/00_baseline_git.log | head -6

{
  echo "# P12-MESH-007 baseline: MESH-006 stability; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  cmake --build build/release -j16 > $HOME/m7_release_build.log 2>&1
  echo "build/release rebuild exit $?; compiled units: $(grep -c 'Building CXX' $HOME/m7_release_build.log); warnings $(grep -c 'warning:' $HOME/m7_release_build.log)"
  echo "cfdapp sha256 $(sha256sum build/release/apps/cli/cfdapp | cut -c1-64) (MESH-006 final: df4ee6d0da11bba722dcb2aea47740875df8c41232d7970cc49f20d10618add6)"
  T=build/release/tests
  run() { out=$($1 --gtest_filter="$2" 2>&1); echo "$(basename $1) '$2': $(echo "$out" | grep -E '^\[  PASSED  \]|^\[  FAILED  \] [0-9]+ test' | tr '\n' ' ') $(echo "$out" | grep -oE 'YOU HAVE [0-9]+ DISABLED' )"; }
  run $T/solver/simple/CFDSimpleTests 'SIMPLE3D.*'
  run $T/unit/io/CFDIoTests 'Case3DTest.*:VTK3DTest.*'
  run $T/unit/discretization/CFDDiscretizationTests 'Operators3DTest.*'
  run $T/unit/mesh/CFDMeshTests 'Cartesian3DMeshTest.*'
  run $T/integration/case/CFDCaseIntegrationTests 'Duct3DProductionCase.*:LidDrivenCube3DProductionCase.*'
  run $T/integration/mms/CFDMMSValidationTests 'MMSSimple3DTest.*:MMS3DTest.*'
  echo "## existing PISO / transient suites (MESH-007 builds on them)"
  for b in $(find $T -type f -perm -u+x -name 'CFD*Tests' | sort); do
    n=$($b --gtest_list_tests 2>/dev/null | grep -cE '^(PISO|Piso|Transient|TimeDerivative|Restart|TemporalRefinement|TransientCavity|TransientPoiseuille)[A-Za-z]*\.')
    [ "$n" -gt 0 ] && run $b 'PISO*:Piso*:Transient*:TimeDerivative*:Restart*:TemporalRefinement*'
  done
  echo "## CLI process tests (2D and 3D)"
  (cd build/release && ctest -R "CFDAppCli|CFDAppSmokeTest" 2>&1 | grep -E "tests passed|tests failed")
  echo "## known pre-existing sanitizer defect (P12-MESH-004 test code), build/asan, CI options"
  out=$(ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
        build/asan/tests/unit/mesh/CFDMeshTests --gtest_filter=MeshQualityReport.DisconnectedMeshIsFatal 2>&1)
  echo "exit $?; $(echo "$out" | grep -E 'SUMMARY: AddressSanitizer' | cut -c1-120)"
  echo "$out" | grep -E "test_mesh_quality_report.cpp:4[0-9][0-9]" | head -2
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L/01_baseline_mesh006_stability.log 2>&1
cat $L/01_baseline_mesh006_stability.log

# ---- 4. pre-MESH-007 reference tree -----------------------------------------------------------------
rm -rf $B; mkdir -p $B
{
  echo "# P12-MESH-007 reference tree (BASE) = the working tree before any MESH-007 change; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git ls-files -co --exclude-standard -> $B"
  git ls-files -co --exclude-standard -z | xargs -0 -I{} cp --parents -p {} $B/ 2>&1 | head
  echo "files copied: $(cd $B && find . -type f | wc -l)"
  (cd $B && find src include apps tests -type f | sort | xargs sha256sum) > $HOME/m7ref/base.src.sha256
  echo "src/include/apps/tests files hashed: $(wc -l < $HOME/m7ref/base.src.sha256) -> \$HOME/m7ref/base.src.sha256"
  cd $B
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCFDAPP_BUILD_GUI=OFF \
    -DCFDAPP_ENABLE_CUDA=OFF -DCFDAPP_ENABLE_OPENMP=OFF -DCFDAPP_ENABLE_MPI=OFF -DCFDAPP_BUILD_BENCHMARKS=ON \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$R/build/release/_deps/nlohmann_json-src \
    -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$R/build/release/_deps/googletest-src > $HOME/m7ref/base.configure.log 2>&1
  echo "configure exit $?"
  cmake --build build -j16 > $HOME/m7ref/base.build.log 2>&1
  echo "BASE build exit $?; warnings $(grep -c 'warning:' $HOME/m7ref/base.build.log)"
  echo "BASE cfdapp sha256 $(sha256sum build/apps/cli/cfdapp | cut -c1-64)"
  echo "NEW  cfdapp sha256 $(sha256sum $R/build/release/apps/cli/cfdapp | cut -c1-64) (identical expected: same sources)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L/02_baseline_reference_tree.log 2>&1
cat $L/02_baseline_reference_tree.log

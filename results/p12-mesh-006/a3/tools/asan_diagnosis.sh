#!/usr/bin/env bash
# P12-MESH-006: diagnosis of the 4 failures of the supplementary ASan+UBSan run (logs/18). Evidence
# only: no source, test, threshold or timeout in the repository is changed. logs/18 stays as written.
#
#  (a) MeshQualityReport.DisconnectedMeshIsFatal -- heap-use-after-free (logs/18). Is it pre-existing?
#      1. byte identity of the test file and the MeshQuality sources with BASE ($HOME/m6ref/base, the
#         pre-MESH-006 working tree whose Release cfdapp is byte-identical to MESH-005's final binary);
#      2. BASE configured with the asan preset's settings (Debug, CFDAPP_ENABLE_SANITIZERS=ON, GUI off,
#         Ninja; dependency sources from BASE's own build, no network) in $HOME/m6ref/base_asan, the
#         mesh test binary built, and the same single test run in BASE and in NEW (build/asan).
#  (b) the 3 Timeouts. logs/18 used --timeout 1800 (full_regression.sh); the repository's CI sanitizer
#      job uses --timeout 7200 because these tests took 1423-2220 s under ASan locally at the P12-NUM
#      closeout (.github/workflows/ci.yml, "Test under ASan+UBSan"). Rerun the three in NEW build/asan
#      with the CI job's own ASAN/UBSAN options and --timeout 7200, the three together (-j3), nothing
#      else running.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BT=$HOME/m6ref/base
BA=$HOME/m6ref/base_asan
L=$R/results/p12-mesh-006/a3/logs
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=0
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
T=MeshQualityReport.DisconnectedMeshIsFatal
{
  echo "# P12-MESH-006 ASan diagnosis (a): $T, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# ASAN_OPTIONS=$ASAN_OPTIONS UBSAN_OPTIONS=$UBSAN_OPTIONS (the CI sanitizer job's)"
  echo "## 1. byte identity with BASE (pre-MESH-006)"
  for f in tests/unit/mesh/test_mesh_quality_report.cpp src/mesh/MeshQuality.cpp include/cfd/mesh/MeshQuality.hpp; do
    if cmp -s $R/$f $BT/$f; then v=IDENTICAL; else v=DIFFERENT; fi
    echo "$v  $f  NEW $(sha256sum $R/$f | cut -c1-16)  BASE $(sha256sum $BT/$f | cut -c1-16)"
  done
  echo "## git: tracked/untracked state of the test file (HEAD $(cd $R && git rev-parse --short HEAD))"
  (cd $R && git status --short -- tests/unit/mesh/test_mesh_quality_report.cpp src/mesh/MeshQuality.cpp include/cfd/mesh/MeshQuality.hpp)
  (cd $R && git ls-files --error-unmatch tests/unit/mesh/test_mesh_quality_report.cpp > /dev/null 2>&1 && echo "tracked at HEAD" || echo "untracked at HEAD (created after HEAD, in the uncommitted P12-MESH work)")
  echo "## the test code (NEW, lines 484-491)"
  sed -n '484,491p' $R/tests/unit/mesh/test_mesh_quality_report.cpp
  echo "## 2. BASE with the asan preset's settings: configure + build CFDMeshTests"
  rm -rf $BA
  cmake -S $BT -B $BA -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCFDAPP_ENABLE_SANITIZERS=ON -DCFDAPP_BUILD_GUI=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$BT/build/_deps/nlohmann_json-src \
        -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$BT/build/_deps/googletest-src > $BA.configure.log 2>&1
  echo "configure exit $?; $(grep -E 'Sanitizer' $BA.configure.log)"
  cmake --build $BA -j16 --target CFDMeshTests > $BA.build.log 2>&1
  echo "build exit $?; warnings $(grep -c 'warning:' $BA.build.log)"
  echo "BASE flags: $(grep -o -- '-fsanitize=[a-z,]*' $BA/compile_commands.json | sort -u | tr '\n' ' ')"
  echo "NEW  flags: $(grep -o -- '-fsanitize=[a-z,]*' $R/build/asan/compile_commands.json | sort -u | tr '\n' ' ')"
  for side in BASE NEW; do
    if [ $side = BASE ]; then bin=$BA/tests/unit/mesh/CFDMeshTests; else bin=$R/build/asan/tests/unit/mesh/CFDMeshTests; fi
    out=$($bin --gtest_filter=$T 2>&1); rc=$?
    echo "=== $side ($bin): exit $rc"
    echo "$out" | grep -E "ERROR: AddressSanitizer|READ of size|SUMMARY:|test_mesh_quality_report.cpp:4[0-9][0-9]|MeshQuality.cpp:[0-9]+|MeshQuality.hpp:[0-9]+|\[  (PASSED|FAILED) |\[       OK \]" | sed 's/^/    /'
  done
  echo "## the same test in NEW with the rest of its suite, not under ASan (logs/16, 17): Passed (use-after-free is silent without ASan)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L/18a_asan_diag_use_after_free.log 2>&1
tail -20 $L/18a_asan_diag_use_after_free.log

{
  echo "# P12-MESH-006 ASan diagnosis (b): the 3 tests that hit --timeout 1800 in logs/18, rerun in NEW"
  echo "# build/asan with the CI sanitizer job's settings (--timeout 7200, ASAN/UBSAN options), -j3, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# ASAN_OPTIONS=$ASAN_OPTIONS UBSAN_OPTIONS=$UBSAN_OPTIONS"
  echo "# build/asan binaries are those of logs/18 (not rebuilt): CFDValidationTests $(sha256sum $(find $R/build/asan/tests -name 'CFDValidation*Tests' -type f | head -1) 2>/dev/null | cut -c1-16)"
  echo "# other processes using CPU at start: $(ps -eo pcpu,comm --sort=-pcpu | awk 'NR>1 && $1>5' | wc -l)"
  (cd $R/build/asan && ctest -R '^(NaturalConvectionValidation\.GridConvergence|CompressibleCoupledProductionCaseTest\.RepeatedRunIsDeterministic|SIMPLEMMS\.DistortedMesh)$' \
     -j3 --timeout 7200 --output-on-failure 2>&1)
  echo "ctest exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L/18b_asan_rerun_timeouts_ci_timeout.log 2>&1
echo "# sanitizer diagnostics in 18b: AddressSanitizer $(grep -c 'ERROR: AddressSanitizer' $L/18b_asan_rerun_timeouts_ci_timeout.log), UBSan runtime errors $(grep -c 'runtime error:' $L/18b_asan_rerun_timeouts_ci_timeout.log), LeakSanitizer $(grep -c 'ERROR: LeakSanitizer' $L/18b_asan_rerun_timeouts_ci_timeout.log)" >> $L/18b_asan_rerun_timeouts_ci_timeout.log
grep -E "Test +#|tests passed|ctest exit|sanitizer diagnostics" $L/18b_asan_rerun_timeouts_ci_timeout.log

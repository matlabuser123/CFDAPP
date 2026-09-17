#!/usr/bin/env bash
# A2 step 2: format the added test file (layout-only proof), rebuild build/release, check the
# library hash and staleness, run the new tests and CFDDiscretizationTests.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
F=tests/unit/discretization/test_gradient_boundary_consistency.cpp
LOG=$P/logs/01_step2_build_focused.log
cd $R || exit 1
{
  echo "# A2 step 2 $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "CMakeLists.txt: $(sha256sum tests/unit/discretization/CMakeLists.txt | cut -d' ' -f1)"
  mkdir -p $P/data/before/tests/unit/discretization
  cp $F $P/data/before/$F
  echo "test file before format: $(sha256sum $F | cut -d' ' -f1) (frozen candidate 3868440b...)"
  clang-format-18 -i $F
  echo "test file after format:  $(sha256sum $F | cut -d' ' -f1)"
  python3 $R/results/p12-diff-002/format-001/tools/token_proof.py $P/data/before $R $F
  echo "clang-format-18 --dry-run --Werror over CI scope: $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 | xargs -0 clang-format-18 --dry-run --Werror 2>&1 | grep -c 'error:') violations"
  t0=$(date +%s)
  cmake --build build/release -j16 > $HOME/g2/step2_build.txt 2>&1 || { tail -30 $HOME/g2/step2_build.txt; echo "BUILD FAILED"; exit 1; }
  echo "build OK ($(( $(date +%s) - t0 )) s), compiled units $(grep -c 'Building CXX' $HOME/g2/step2_build.txt), warnings $(grep -c 'warning:' $HOME/g2/step2_build.txt)"
  grep -m3 'warning:' $HOME/g2/step2_build.txt
  echo "libcfdcore.a $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1) (must be 143a1dda...)"
  NT=$(find src include tests apps -type f \( -name '*.cpp' -o -name '*.hpp' \) -printf '%T@\n' | sort -rn | head -1)
  STALE=0
  while read -r t bin; do awk -v a="$t" -v b="$NT" 'BEGIN{exit !(a<b)}' && { STALE=$((STALE+1)); echo "STALE $bin"; }; done \
    < <(find build/release -type f \( -name 'CFD*Tests' -o -name 'cfdapp*' \) -perm -u+x -printf '%T@ %p\n')
  echo "stale executables: $STALE"
  B=build/release/tests/unit/discretization/CFDDiscretizationTests
  echo "## new tests"
  $B --gtest_filter='GradientBoundaryConsistency.*' 2>&1 | grep -E '^\[ +(OK|FAILED|PASSED) |tests ran'
  echo "## CFDDiscretizationTests (whole binary)"
  $B 2>&1 | grep -E '^\[  (PASSED|FAILED)  \]|tests ran|YOU HAVE'
  echo "exit ${PIPESTATUS[0]}"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG

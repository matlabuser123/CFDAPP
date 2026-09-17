#!/usr/bin/env bash
# P12-MESH-007 G10.1: focused MESH-007 suites and the dependent suites, staged, with exact passed,
# failed and disabled counts; fresh Release binaries (the GRAD-002 A3 regression's clean-first
# build/release, unchanged since), failing closed on a stale binary. Stage 2 selects the dependent
# suites the way logs/17 did (the modules MESH-007 touched), now as whole executables.
# Stops at the first failing stage.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$R/build/release/tests
LOG=$R/results/p12-mesh-007/logs/61_g10_1_focused.log
[ -e $LOG ] && { echo "REFUSED: $LOG exists"; exit 1; }
cd $R || exit 1
fail=0
run() {  # binary filter
  local out; out=$($1 --gtest_filter="$2" 2>&1); local rc=$?
  local passed failed disabled
  passed=$(echo "$out" | sed -n 's/^\[  PASSED  \] \([0-9]*\) tests\?\./\1/p')
  failed=$(echo "$out" | sed -n 's/^\[  FAILED  \] \([0-9]*\) tests\?, listed below:/\1/p')
  disabled=$(echo "$out" | sed -n 's/.*YOU HAVE \([0-9]*\) DISABLED TESTS\?.*/\1/p')
  echo "$(basename $1) '$2': passed ${passed:-0}, failed ${failed:-0}, disabled ${disabled:-0}, exit $rc"
  [ $rc = 0 ] && [ -n "$passed" ] && [ "${passed:-0}" -gt 0 ] || { fail=1; echo "$out" | grep -E '^\[  FAILED  \]|Failure' | head -20; }
}
{
  echo "# P12-MESH-007 G10.1 $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
  echo "# libcfdcore.a $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
  NT=$(find src include tests apps -type f \( -name '*.cpp' -o -name '*.hpp' \) -printf '%T@\n' | sort -rn | head -1)
  STALE=0
  while read -r t bin; do awk -v a="$t" -v b="$NT" 'BEGIN{exit !(a<b)}' && { STALE=$((STALE+1)); echo "STALE $bin"; }; done \
    < <(find $T -type f -name 'CFD*Tests' -perm -u+x -printf '%T@ %p\n')
  echo "# test executables older than the newest source: $STALE"
  [ $STALE = 0 ] || { echo "FAIL CLOSED: stale binaries"; exit 1; }
  echo "## stage 1: the MESH-007 suites"
  run $T/unit/mesh/CFDMeshTests 'MeshMotion*:MeshGeometryUpdate*'
  run $T/unit/discretization/CFDDiscretizationTests 'AleOperators*'
  run $T/solver/piso/CFDPisoTests 'AlePiso*'
  [ $fail = 0 ] || { echo "STOP: stage 1 failed"; exit 1; }
  echo "## stage 2: the dependent suites (whole executables of the touched modules, and the PISO / transient / restart tests elsewhere)"
  run $T/solver/piso/CFDPisoTests '*'
  run $T/unit/solver/CFDSolverTests '*'
  run $T/unit/mesh/CFDMeshTests '*'
  run $T/unit/discretization/CFDDiscretizationTests '*'
  for b in $(find $T -type f -perm -u+x -name 'CFD*Tests' | sort); do
    case $(basename $b) in CFDPisoTests|CFDSolverTests|CFDMeshTests|CFDDiscretizationTests) continue;; esac
    n=$($b --gtest_list_tests 2>/dev/null | grep -cE '^(PISO|Piso|Transient|TimeDerivative|Restart|TemporalRefinement)[A-Za-z]*\.')
    [ "$n" -gt 0 ] && run $b 'PISO*:Piso*:Transient*:TimeDerivative*:Restart*:TemporalRefinement*'
  done
  [ $fail = 0 ] || { echo "STOP: stage 2 failed"; exit 1; }
  echo "G10.1 PASS"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG

#!/usr/bin/env bash
# P12-GRAD-002 A1: focused test suites, then the full regression (authorization items 9 and C13).
# Usage: a1_tests.sh focused | full | gui
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002
B=$R/build/release
mkdir -p $P/a1/logs

case ${1:-focused} in
focused)
  L=$P/a1/logs/10_focused_new.log
  {
    echo "# P12-GRAD-002 A1 focused suites; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# libcfdcore $(sha256sum $B/src/libcfdcore.a | cut -c1-16) cfdapp $(sha256sum $B/apps/cli/cfdapp | cut -c1-16)"
    for t in tests/unit/discretization/CFDDiscretizationTests \
             tests/unit/mesh/CFDMeshTests \
             tests/solver/piso/CFDPisoTests \
             tests/solver/simple/CFDSolverTests \
             tests/unit/core/CFDCoreTests \
             tests/unit/fields/CFDFieldTests \
             tests/unit/algebra/CFDAlgebraTests \
             tests/unit/thermal/CFDThermalTests \
             tests/unit/turbulence/CFDTurbulenceTests \
             tests/integration/mms/CFDMMSValidationTests \
             tests/integration/case/CFDCaseIntegrationTests; do
      name=$(basename "$t")
      if [ -x "$B/$t" ]; then
        out=$("$B/$t" 2>&1 | grep -E "^\[==========\] .* tests? from .* ran|^\[  PASSED  \]|^\[  FAILED  \]" | tr '\n' ' ')
        echo "$name: $out"
      else
        echo "$name: NOT BUILT"
      fi
    done
  } > $L 2>&1
  cat $L
  ;;
full)
  L=$P/a1/logs/11_regression_release_new.log
  {
    echo "# P12-GRAD-002 A1 full regression, Release; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "# libcfdcore $(sha256sum $B/src/libcfdcore.a | cut -c1-16) cfdapp $(sha256sum $B/apps/cli/cfdapp | cut -c1-16)"
    cd $B && ctest --output-on-failure --timeout 7200 2>&1 | tail -40
  } > $L 2>&1
  tail -25 $L
  ;;
esac

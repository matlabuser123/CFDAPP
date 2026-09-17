#!/usr/bin/env bash
# P12-MESH-007 state at the stop (after the G6.3 failure) -- INFORMATION, not G9/G10 gate runs:
# the existing suites of the modules MESH-007 touched (PISO refactor, TransientSolver hook, Mesh/Cell/Face
# setters, MeshGeometry additions, TimeDerivative/MassFlux additions), Release binaries of the gate run;
# and a clang-format-18 dry run of the new and changed C++ files (no file is reformatted).
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$R/build/release/tests
L=$R/results/p12-mesh-007/logs/17_stop_state_existing_suites_NOT_gate.log
cd $R
run() { out=$($1 --gtest_filter="$2" 2>&1); echo "$(basename $1) '$2': $(echo "$out" | grep -E '^\[  PASSED  \]|^\[  FAILED  \] [0-9]+ test' | tr '\n' ' ') $(echo "$out" | grep -oE 'YOU HAVE [0-9]+ DISABLED')"; }
{
  echo "# P12-MESH-007 stop-state checks (information, NOT G9/G10); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  run $T/solver/piso/CFDPisoTests '-AlePiso*'
  run $T/unit/solver/CFDSolverTests '*'
  run $T/unit/mesh/CFDMeshTests '-MeshMotion*:MeshGeometryUpdate*'
  run $T/unit/discretization/CFDDiscretizationTests '-AleOperators*'
  for b in $(find $T -type f -perm -u+x -name 'CFD*Tests' | sort); do
    case $(basename $b) in CFDPisoTests|CFDSolverTests|CFDMeshTests|CFDDiscretizationTests) continue;; esac
    n=$($b --gtest_list_tests 2>/dev/null | grep -cE '^(PISO|Piso|Transient|TimeDerivative|Restart|TemporalRefinement)[A-Za-z]*\.')
    [ "$n" -gt 0 ] && run $b 'PISO*:Piso*:Transient*:TimeDerivative*:Restart*:TemporalRefinement*'
  done
  echo "## clang-format-18 --dry-run of the new and changed C++ files (not reformatted)"
  for f in include/cfd/mesh/Cell.hpp include/cfd/mesh/Face.hpp include/cfd/mesh/Mesh.hpp src/mesh/Mesh.cpp \
           include/cfd/mesh/MeshGeometry.hpp src/mesh/MeshGeometry.cpp include/cfd/mesh/MeshMotion.hpp \
           src/mesh/MeshMotion.cpp include/cfd/discretization/TimeDerivative.hpp src/discretization/TimeDerivative.cpp \
           include/cfd/physics/MassFlux.hpp src/physics/MassFlux.cpp include/cfd/pressure_velocity/TransientMomentum.hpp \
           src/pressure_velocity/TransientMomentum.cpp src/pressure_velocity/PisoStep.hpp src/pressure_velocity/PISO.cpp \
           include/cfd/pressure_velocity/AlePISO.hpp src/pressure_velocity/AlePISO.cpp include/cfd/solver/TransientSolver.hpp \
           src/solver/TransientSolver.cpp tests/support/MeshMotionCases.hpp tests/unit/mesh/test_mesh_motion.cpp \
           tests/unit/discretization/test_ale_operators.cpp tests/solver/piso/test_ale_piso.cpp; do
    clang-format-18 --dry-run --Werror "$f" > /dev/null 2>&1 && echo "clean      $f" || echo "WOULD CHANGE $f"
  done
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L 2>&1
cat $L

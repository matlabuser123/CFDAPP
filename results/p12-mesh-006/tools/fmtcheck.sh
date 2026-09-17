#!/usr/bin/env bash
# clang-format-18 dry run (no changes) over the MESH-006 new/modified C++ files.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $R
files="apps/cli/main.cpp include/cfd/discretization/Interpolation.hpp include/cfd/discretization/VectorGradient.hpp
include/cfd/io/CSVWriter.hpp include/cfd/io/case/GeometryConfig.hpp include/cfd/io/case/InitialConditions.hpp
include/cfd/io/case/MeshConfig.hpp include/cfd/io/case/SolverConfig.hpp include/cfd/physics/ContinuityEquation.hpp
include/cfd/physics/MomentumEquation.hpp include/cfd/pressure_velocity/PressureCorrectionEquation.hpp
include/cfd/pressure_velocity/RelaxedMomentum.hpp include/cfd/pressure_velocity/SIMPLEProgress.hpp
include/cfd/pressure_velocity/SIMPLEResult.hpp include/cfd/pressure_velocity/SIMPLESettings.hpp
include/cfd/pressure_velocity/RhieChow.hpp include/cfd/solver/SolverRobustness.hpp src/app/ProjectRunner.cpp
src/compressible/CompressibleMassFlux.cpp src/compressible/CompressibleMomentum.cpp
src/compressible/CompressiblePressureCorrection.cpp src/compressible/CompressibleRelaxedMomentum.cpp
src/compressible/CompressibleSIMPLE.cpp src/discretization/Interpolation.cpp src/discretization/VectorGradient.cpp
src/io/CSVWriter.cpp src/io/CaseBuilder.cpp src/io/CaseReader.cpp src/io/CaseWriter.cpp src/io/JSONWriter.cpp
src/io/ResultExporter.cpp src/io/case/BoundaryConfigParser.cpp src/io/case/CaseConfigParser.cpp
src/io/case/GeometryConfigParser.cpp src/io/case/JsonUtil.cpp src/io/case/JsonUtil.hpp
src/io/case/MeshConfigParser.cpp src/io/case/Parsers.hpp src/io/case/SolverConfigParser.cpp
src/physics/ContinuityEquation.cpp src/physics/MomentumEquation.cpp src/pressure_velocity/PISO.cpp
src/pressure_velocity/PressureCorrectionEquation.cpp src/pressure_velocity/RelaxedMomentum.cpp
src/pressure_velocity/RhieChow.cpp src/pressure_velocity/SIMPLE.cpp src/pressure_velocity/SIMPLESettings.cpp
src/pressure_velocity/TransientMomentum.cpp src/solver/SolverRobustness.cpp src/turbulence/KEpsilonModel.cpp
src/turbulence/KOmegaModel.cpp tests/unit/discretization/test_operators3d.cpp tests/solver/simple/test_simple3d.cpp
tests/integration/mms/test_mms_simple3d.cpp tests/integration/case/test_3d_production_cases.cpp
tests/unit/io/test_case3d.cpp"
bad=0; total=0
for f in $files; do
  total=$((total+1))
  n=$(clang-format-18 --dry-run "$f" 2>&1 | grep -c "warning:")
  if [ "$n" -gt 0 ]; then bad=$((bad+1)); echo "$f: $n formatting differences"; fi
done
echo "clang-format-18 dry run: $bad of $total files would change"

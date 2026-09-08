#pragma once

#include "cfd/io/case/BoundaryConfig.hpp"
#include "cfd/io/case/CaseConfig.hpp"
#include "cfd/io/case/GeometryConfig.hpp"
#include "cfd/io/case/InitialConditions.hpp"
#include "cfd/io/case/MeshConfig.hpp"
#include "cfd/io/case/PhysicsConfig.hpp"
#include "cfd/io/case/SolverConfig.hpp"

namespace cfd::io {

// The fully parsed and validated case, as CaseReader::read() returns it
// (TODO.md P1 section 5). Every field has already passed both per-file
// and cross-file validation -- a CaseBuilder consuming this never needs
// to re-check "is this actually valid", only "how do I construct the
// runtime object this describes".
struct CaseDefinition {
  CaseConfig caseConfig;
  GeometryConfig geometry;
  MeshConfig mesh;
  PhysicsConfig physics;
  BoundaryConfig boundaries;
  SolverConfig solver;
  InitialConditions initialConditions;
};

}  // namespace cfd::io

#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"

namespace cfd::io {

// The bridge between configuration and CFD execution (TODO.md P1 section
// 24): everything a solver needs, already built as the actual runtime
// types SIMPLE::solve() takes -- not a pre-constructed SIMPLE itself
// (section 25: CaseReader/CaseBuilder must not hand back an
// already-running solver). The reference cell (pressure null-space gauge)
// is deliberately not part of this -- it stays the deterministic default
// 0 a caller passes directly to SIMPLE's constructor (section 43).
struct SimulationSetup {
  cfd::mesh::Mesh mesh;
  cfd::physics::FluidProperties fluid;
  cfd::boundary::BoundaryConditionSet velocityBoundaries;
  cfd::boundary::BoundaryConditionSet pressureBoundaries;
  cfd::pressure_velocity::SIMPLESettings solverSettings;
  cfd::fields::VectorField initialVelocity;
  cfd::fields::ScalarField initialPressure;
};

}  // namespace cfd::io

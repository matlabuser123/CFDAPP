#include "cfd/io/CaseBuilder.hpp"

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::io {

using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::MovingWall;
using cfd::boundary::Outlet;
using cfd::boundary::Symmetry;
using cfd::boundary::Wall;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLESettings;

namespace {

// Both branches below only ever see a type string CaseReader's own
// parsing already restricted to the supported set -- the default clauses
// exist so a future addition to BoundaryConfig's supported-type list
// can't silently fall through here unnoticed, not because a
// CaseDefinition CaseBuilder receives is expected to carry anything else.
std::unique_ptr<cfd::boundary::VectorBoundaryCondition> buildVelocityBoundary(
    const VelocityBoundarySpec& spec) {
  if (spec.type == "wall") return std::make_unique<Wall>();
  if (spec.type == "moving_wall") return std::make_unique<MovingWall>(spec.value);
  if (spec.type == "inlet") return std::make_unique<Inlet>(spec.value);
  if (spec.type == "outlet") return std::make_unique<Outlet>();
  if (spec.type == "symmetry") return std::make_unique<Symmetry>();
  throw CaseConfigurationError("CaseBuilder: unsupported velocity boundary type \"" + spec.type +
                               "\"");
}

std::unique_ptr<cfd::boundary::ScalarBoundaryCondition> buildPressureBoundary(
    const PressureBoundarySpec& spec) {
  if (spec.type == "fixed_value") return std::make_unique<FixedValue>(spec.value);
  if (spec.type == "fixed_gradient") return std::make_unique<FixedGradient>(spec.value);
  throw CaseConfigurationError("CaseBuilder: unsupported pressure boundary type \"" + spec.type +
                               "\"");
}

SIMPLESettings buildSolverSettings(const SolverConfig& solver) {
  SIMPLESettings settings;
  settings.maxIterations = solver.maxIterations;
  settings.velocityRelaxation = solver.velocityRelaxation;
  settings.pressureRelaxation = solver.pressureRelaxation;
  settings.velocityTolerance = solver.velocityTolerance;
  settings.pressureTolerance = solver.pressureTolerance;
  settings.continuityTolerance = solver.continuityTolerance;
  settings.momentumSolver.absoluteTolerance = solver.momentumSolver.absoluteTolerance;
  settings.momentumSolver.relativeTolerance = solver.momentumSolver.relativeTolerance;
  settings.momentumSolver.maxIterations = solver.momentumSolver.maxIterations;
  settings.pressureSolver.absoluteTolerance = solver.pressureSolver.absoluteTolerance;
  settings.pressureSolver.relativeTolerance = solver.pressureSolver.relativeTolerance;
  settings.pressureSolver.maxIterations = solver.pressureSolver.maxIterations;
  return settings;
}

}  // namespace

SimulationSetup CaseBuilder::build(const CaseDefinition& definition) const {
  // Only geometry=rectangle + mesh=structured_cartesian exist in this
  // phase's supported set (CaseReader's parsers already reject anything
  // else), so this is the only construction path -- not a dispatch that
  // needs its own "unsupported combination" branch (TODO.md P1 section
  // 42's "geometry dimensions incompatible with mesh configuration" cross
  // check has nothing left to check once both are already this
  // constrained).
  Mesh mesh =
      MeshGeometry::createCartesian2D(definition.mesh.nx, definition.mesh.ny,
                                      definition.geometry.length, definition.geometry.height);
  FluidProperties fluid(definition.physics.density, definition.physics.dynamicViscosity);

  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  // Built in mesh patch order (TODO.md P1 section 39), looking each
  // patch up by name in the (deterministically-ordered, std::map-backed)
  // BoundaryConfig rather than iterating boundaries.json's own object --
  // this is also where CaseReader's cross-file guarantee ("every mesh
  // patch has a configured entry") gets exercised: .at() below cannot
  // throw std::out_of_range because of that guarantee, not because of
  // anything checked again here.
  for (const auto& patch : mesh.boundaryPatches()) {
    const PatchBoundaryConfig& patchConfig = definition.boundaries.patches.at(patch.name());
    velocityBoundaries.set(mesh, patch.name(), buildVelocityBoundary(patchConfig.velocity));
    pressureBoundaries.set(mesh, patch.name(), buildPressureBoundary(patchConfig.pressure));
  }

  const cfd::Index numberOfCells = mesh.numberOfCells();
  cfd::fields::VectorField initialVelocity(numberOfCells, definition.initialConditions.velocity);
  cfd::fields::ScalarField initialPressure(numberOfCells, definition.initialConditions.pressure);

  return SimulationSetup{std::move(mesh),
                         fluid,
                         std::move(velocityBoundaries),
                         std::move(pressureBoundaries),
                         buildSolverSettings(definition.solver),
                         std::move(initialVelocity),
                         std::move(initialPressure)};
}

}  // namespace cfd::io

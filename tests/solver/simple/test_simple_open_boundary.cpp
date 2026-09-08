// This session added Dirichlet (FixedValue) pressure-boundary support to
// PressureCorrectionEquation specifically to unblock cases like this one:
// TODO.md P0 -- SIMPLE section 14 and test_simple_continuity.cpp's header
// comment both document that a prior session found an open (inlet/outlet)
// channel diverges under the *original* all-zero-coupling boundary
// treatment, since a fully-Neumann pressure-correction system has no
// degree of freedom to absorb a global mass-flow mismatch. A fixed-
// pressure outlet supplies exactly that degree of freedom. This test is
// the load-bearing, end-to-end proof that the fix works through the full
// SIMPLE loop, not just at the assembly level -- see
// PressureCorrectionTest.OpenBoundaryDirichletCouplingMatchesHandDerivation
// in test_pressure_correction.cpp for the unit-level hand-derivation this
// builds on.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

BoundaryConditionSet makeChannelVelocityBoundaries(const Mesh& mesh, Vector2 inletVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(inletVelocity));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

// Neumann everywhere except a Dirichlet (fixed-pressure) outlet -- the
// open-boundary treatment this session added.
BoundaryConditionSet makeChannelPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

SIMPLESettings makeChannelSettings() {
  SIMPLESettings settings;
  settings.maxIterations = 3000;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  return settings;
}

}  // namespace

TEST(SIMPLEOpenBoundaryTest, OpenChannelConverges) {
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 4, 2.0, 1.0);
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeChannelPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.1);  // Re = rho*U*H/mu = 10.

  const SIMPLE simple(makeChannelSettings(), /*referenceCell=*/0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  // The headline claim: this used to diverge (PressureCorrectionFailure
  // or NonFiniteState under the original zero-everywhere treatment); it
  // must now actually converge.
  ASSERT_EQ(result.status, SIMPLEStatus::Converged)
      << "status=" << static_cast<int>(result.status) << " iterations=" << result.iterations;

  for (Index i = 0; i < result.velocity.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.velocity[i].x));
    EXPECT_TRUE(std::isfinite(result.velocity[i].y));
  }
  for (Index i = 0; i < result.pressure.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.pressure[i]));
  }

  // Global mass balance: inflow must match outflow (TODO.md section 14) --
  // exactly the degree of freedom a closed/Neumann domain does not have.
  EXPECT_NEAR(result.globalMassImbalance, 0.0, 1e-6);

  // No leakage through the walls (the outlet itself is expected to carry
  // real, non-zero flux -- only top/bottom are impermeable here).
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() != "top" && patch.name() != "bottom") continue;
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(result.massFlux[faceId], 0.0, 1e-9);
    }
  }

  // Sanity: net flow should be in the +x direction (inlet pushes right),
  // not a solver artifact settling on the trivial zero solution.
  Real outletFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    if (patch.name() != "right") continue;
    for (const Index faceId : patch.faceIds()) outletFlux += result.massFlux[faceId];
  }
  EXPECT_GT(outletFlux, 0.5);
}

TEST(SIMPLEOpenBoundaryTest, OpenChannelIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 4, 2.0, 1.0);
  const auto velocityBoundaries = makeChannelVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeChannelPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.1);
  const SIMPLE simple(makeChannelSettings(), /*referenceCell=*/0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult a = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                      initialVelocity, initialPressure);
  const SIMPLEResult b = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                      initialVelocity, initialPressure);

  ASSERT_EQ(a.status, SIMPLEStatus::Converged);
  ASSERT_EQ(a.status, b.status);
  ASSERT_EQ(a.iterations, b.iterations);
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y);
  }
  for (Index i = 0; i < a.pressure.size(); ++i) EXPECT_EQ(a.pressure[i], b.pressure[i]);
}

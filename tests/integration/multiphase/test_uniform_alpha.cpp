// P3-PHYS-005 sections 25-26: uniform-field and zero-velocity
// preservation, mandatory regressions, exercised over *multiple*
// timesteps (the unit-level VolumeFractionSolverTest suite already
// proves both identities for a single step -- this file proves they
// hold under repeated stepping too, which a single-step proof does not
// automatically guarantee for an iterative linear solver in floating
// point).
#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/multiphase/VolumeFractionSolver.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

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
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::multiphase::VolumeFractionSolver;
using cfd::multiphase::VolumeFractionSolverSettings;
using cfd::multiphase::VolumeFractionStatus;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

VolumeFractionSolverSettings makeLoosenedSettings() {
  // See test_volume_fraction_solver.cpp's own header comment on the
  // diagnosed unpreconditioned-BiCGSTAB-on-a-pure-advection-operator
  // breakdown -- same fix (small dt for diagonal dominance, a tolerance
  // above the practical floor that breakdown still lets it reach).
  VolumeFractionSolverSettings settings;
  settings.linearSolver.absoluteTolerance = 1e-4;
  settings.linearSolver.relativeTolerance = 1e-3;
  return settings;
}

}  // namespace

TEST(UniformAlphaPreservationTest, DivergenceFreeChannelFlowPreservesUniformAlphaOverManySteps) {
  const Real length = 1.0, height = 0.2, u = 1.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 4, length, height);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{u, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  BoundaryConditionSet alphaBoundaries;
  const Real alphaValue = 0.42;
  alphaBoundaries.set(mesh, "left", std::make_unique<FixedValue>(alphaValue));
  alphaBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));

  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{u, 0.0});
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  ScalarField alpha(n, alphaValue);  // already uniform and consistent with the inlet value.
  const VolumeFractionSolver solver{makeLoosenedSettings()};
  const Real dt = 0.005;
  for (int step = 0; step < 20; ++step) {
    const auto result = solver.step(mesh, alpha, massFlux, alphaBoundaries, dt);
    ASSERT_EQ(result.status, VolumeFractionStatus::Converged) << "step " << step;
    alpha = result.alpha;
  }

  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(alpha[i], alphaValue, 1e-6) << "cell " << i;
  }
}

TEST(UniformAlphaPreservationTest, ZeroVelocityPreservesANonUniformFieldOverManySteps) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  BoundaryConditionSet alphaBoundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    alphaBoundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  const Index n = mesh.numberOfCells();
  ScalarField alpha(n);
  for (Index i = 0; i < n; ++i) alpha[i] = 0.1 + 0.06 * static_cast<Real>(i % 5);
  const ScalarField initialAlpha = alpha;
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);  // u=v=0 everywhere.

  const VolumeFractionSolver solver{};
  for (int step = 0; step < 10; ++step) {
    const auto result = solver.step(mesh, alpha, massFlux, alphaBoundaries, 0.02);
    ASSERT_EQ(result.status, VolumeFractionStatus::Converged) << "step " << step;
    alpha = result.alpha;
  }

  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(alpha[i], initialAlpha[i], 1e-9) << "cell " << i;
  }
}

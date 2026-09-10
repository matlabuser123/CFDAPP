// P3-PHYS-005: VolumeFractionSolver -- mirrors test_species_solver.cpp's
// own structure (config-error handling, determinism), plus the
// zero-velocity-preservation regression (section 26) exercised through
// the actual solve, not just the raw assembly.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
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
using cfd::multiphase::VolumeFractionStepResult;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

BoundaryConditionSet makeZeroGradientBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

// Diagnosed: same class of finding as species::SpeciesSolver's own (see
// test_species_advection_diffusion.cpp's header comment) -- unpreconditioned
// BiCGSTAB applied to a pure-advection (no diffusion, section 12) operator
// hits its own breakdown condition (rho_i -> 0) after very few iterations,
// independent of tolerance. Confirmed here (empirically, standalone
// diagnostic) that the breakdown itself still happens even at a small
// Courant number, but only *after* the residual has already collapsed by
// several orders of magnitude (e.g. 0.1 -> ~4e-5 in a single iteration) --
// i.e. it breaks down right at the point of practical convergence, not
// before doing useful work. Loosened here accordingly: a tolerance well
// above that practical floor, reached before breakdown, rather than the
// library default that sits below it.
VolumeFractionSolverSettings makeLoosenedSettings() {
  VolumeFractionSolverSettings settings;
  settings.linearSolver.absoluteTolerance = 1e-4;
  settings.linearSolver.relativeTolerance = 1e-3;
  return settings;
}

}  // namespace

TEST(VolumeFractionSolverTest, ZeroVelocityPreservesAlphaThroughAnActualSolve) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  ScalarField alphaOld(n);
  for (Index i = 0; i < n; ++i) alphaOld[i] = 0.2 + 0.1 * static_cast<Real>(i % 3);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  const VolumeFractionSolver solver{};
  const VolumeFractionStepResult result = solver.step(mesh, alphaOld, massFlux, boundaries, 0.05);
  ASSERT_EQ(result.status, VolumeFractionStatus::Converged);
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(result.alpha[i], alphaOld[i], 1e-9) << "cell " << i;
  }
}

TEST(VolumeFractionSolverTest, ConvergesForASimpleUniformInletFlow) {
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 4, 1.0, 0.2);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  BoundaryConditionSet alphaBoundaries;
  alphaBoundaries.set(mesh, "left", std::make_unique<FixedValue>(1.0));
  alphaBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));

  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField alphaOld(n, 0.0);

  const VolumeFractionSolver solver{makeLoosenedSettings()};
  const VolumeFractionStepResult result =
      solver.step(mesh, alphaOld, massFlux, alphaBoundaries, 0.001);
  if (result.status != VolumeFractionStatus::Converged) {
    ADD_FAILURE() << "status=" << static_cast<int>(result.status)
                  << " linearIter=" << result.linearIterations
                  << " initialResidual=" << result.initialResidual
                  << " finalResidual=" << result.finalResidual;
    return;
  }
  for (Index i = 0; i < n; ++i) {
    EXPECT_GE(result.alpha[i], -1e-6);
    EXPECT_LE(result.alpha[i], 1.0 + 1e-6);
  }
}

TEST(VolumeFractionSolverTest, InvalidConfigurationForMismatchedAlphaSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField alphaOld(mesh.numberOfCells() + 1, 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const VolumeFractionSolver solver{};
  const VolumeFractionStepResult result = solver.step(mesh, alphaOld, massFlux, boundaries, 0.1);
  EXPECT_EQ(result.status, VolumeFractionStatus::InvalidConfiguration);
}

TEST(VolumeFractionSolverTest, InvalidConfigurationForMismatchedMassFluxSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField alphaOld(mesh.numberOfCells(), 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  const VolumeFractionSolver solver{};
  const VolumeFractionStepResult result = solver.step(mesh, alphaOld, massFlux, boundaries, 0.1);
  EXPECT_EQ(result.status, VolumeFractionStatus::InvalidConfiguration);
}

TEST(VolumeFractionSolverTest, InvalidConfigurationForNonPositiveDt) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField alphaOld(mesh.numberOfCells(), 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const VolumeFractionSolver solver{};
  EXPECT_EQ(solver.step(mesh, alphaOld, massFlux, boundaries, 0.0).status,
            VolumeFractionStatus::InvalidConfiguration);
  EXPECT_EQ(solver.step(mesh, alphaOld, massFlux, boundaries, -1.0).status,
            VolumeFractionStatus::InvalidConfiguration);
}

TEST(VolumeFractionSolverTest, NonFiniteStateForNonFiniteAlphaOld) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  ScalarField alphaOld(mesh.numberOfCells(), 0.5);
  alphaOld[0] = std::numeric_limits<Real>::quiet_NaN();
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const VolumeFractionSolver solver{};
  const VolumeFractionStepResult result = solver.step(mesh, alphaOld, massFlux, boundaries, 0.1);
  EXPECT_EQ(result.status, VolumeFractionStatus::NonFiniteState);
}

TEST(VolumeFractionSolverTest, NonFiniteStateForNonFiniteMassFlux) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeZeroGradientBoundaries(mesh);
  const ScalarField alphaOld(mesh.numberOfCells(), 0.5);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[0] = std::numeric_limits<Real>::infinity();
  const VolumeFractionSolver solver{};
  const VolumeFractionStepResult result = solver.step(mesh, alphaOld, massFlux, boundaries, 0.1);
  EXPECT_EQ(result.status, VolumeFractionStatus::NonFiniteState);
}

TEST(VolumeFractionSolverTest, LinearSolveFailureWhenInnerSolverIsStarved) {
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 4, 1.0, 0.2);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  BoundaryConditionSet alphaBoundaries;
  alphaBoundaries.set(mesh, "left", std::make_unique<FixedValue>(1.0));
  alphaBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));

  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField alphaOld(n, 0.0);

  VolumeFractionSolverSettings settings;
  settings.linearSolver.maxIterations = 1;
  settings.linearSolver.absoluteTolerance = 1e-300;
  settings.linearSolver.relativeTolerance = 1e-300;
  const VolumeFractionSolver solver{settings};
  const VolumeFractionStepResult result =
      solver.step(mesh, alphaOld, massFlux, alphaBoundaries, 0.01);
  EXPECT_EQ(result.status, VolumeFractionStatus::LinearSolveFailure);
}

TEST(VolumeFractionSolverTest, RepeatedStepIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 4, 1.0, 0.2);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  BoundaryConditionSet alphaBoundaries;
  alphaBoundaries.set(mesh, "left", std::make_unique<FixedValue>(1.0));
  alphaBoundaries.set(mesh, "right", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  alphaBoundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));

  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});
  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
  const ScalarField alphaOld(n, 0.0);

  const VolumeFractionSolver solver{makeLoosenedSettings()};
  const VolumeFractionStepResult a =
      solver.step(mesh, alphaOld, massFlux, alphaBoundaries, 0.001);
  const VolumeFractionStepResult b =
      solver.step(mesh, alphaOld, massFlux, alphaBoundaries, 0.001);
  ASSERT_EQ(a.status, VolumeFractionStatus::Converged);
  ASSERT_EQ(b.status, VolumeFractionStatus::Converged);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.alpha[i], b.alpha[i]);
  }
}

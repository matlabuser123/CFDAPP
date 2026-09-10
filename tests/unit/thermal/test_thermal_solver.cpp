#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/ThermalProperties.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedTemperature;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalResult;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalSolverSettings;
using cfd::thermal::ThermalStatus;

namespace {

// 1D pure-conduction cavity: left/right FixedTemperature, top/bottom
// Adiabatic, no flow (massFlux == 0), no source. Analytical solution:
// T(x) = Th + (Tc - Th) * x / L, exact for this FVM scheme on an
// orthogonal Cartesian mesh (a linear field has zero second derivative,
// and this discretization is exact for affine fields -- same reasoning
// as the already-verified diffusion operator's analytical-exactness
// tests).
Mesh makeConductionMesh() { return MeshGeometry::createCartesian2D(20, 4, 1.0, 0.2); }

BoundaryConditionSet makeConductionBoundaries(const Mesh& mesh, Real th, Real tc) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(th));
  boundaries.set(mesh, "right", std::make_unique<FixedTemperature>(tc));
  boundaries.set(mesh, "top", std::make_unique<Adiabatic>());
  boundaries.set(mesh, "bottom", std::make_unique<Adiabatic>());
  return boundaries;
}

}  // namespace

TEST(ThermalSolverTest, ConvergesAndMatchesAnalytical1DConductionProfile) {
  const Mesh mesh = makeConductionMesh();
  const Real th = 310.0, tc = 290.0;
  const Real length = 1.0;
  const auto boundaries = makeConductionBoundaries(mesh, th, tc);
  const Index n = mesh.numberOfCells();
  const ScalarField initialTemperature(n, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  const ThermalSolver solver{};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);

  ASSERT_EQ(result.status, ThermalStatus::Converged);
  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.temperature.size(), n);

  Real maxAbsError = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real exact = th + (tc - th) * (cell.centroid().x / length);
    const Real error = std::abs(result.temperature[cell.id()] - exact);
    maxAbsError = std::max(maxAbsError, error);
    // Temperature must stay within [Tc, Th] for this source-free case.
    EXPECT_GE(result.temperature[cell.id()], tc - 1e-6);
    EXPECT_LE(result.temperature[cell.id()], th + 1e-6);
  }
  EXPECT_LT(maxAbsError, 1e-6);
}

TEST(ThermalSolverTest, InvalidConfigurationForMismatchedTemperatureSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells() + 1, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  const ThermalSolver solver{};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::InvalidConfiguration);
  EXPECT_FALSE(result.converged());
}

TEST(ThermalSolverTest, InvalidConfigurationForMismatchedMassFluxSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  const ThermalSolver solver{};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::InvalidConfiguration);
}

TEST(ThermalSolverTest, MaxIterationsWhenOuterBudgetTooSmall) {
  // The Adiabatic top/bottom boundaries need many outer iterations to
  // converge (see ThermalSolverSettings's own header comment) -- a
  // budget of 1 cannot possibly reach the default 1e-8 tolerance for
  // this case, so this must report MaxIterations, never Converged.
  const Mesh mesh = makeConductionMesh();
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  ThermalSolverSettings settings;
  settings.maxIterations = 1;
  const ThermalSolver solver{settings};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::MaxIterations);
  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.iterations, 1u);
}

TEST(ThermalSolverTest, LinearSolveFailureWhenInnerSolverIsStarved) {
  // A single BiCGSTAB iteration with an unreachable tolerance cannot
  // converge -- mirrors the "starved solver" precedent
  // pressure_velocity::SIMPLE's own test_simple_failure.cpp uses to
  // exercise its MomentumFailure/PressureCorrectionFailure statuses.
  const Mesh mesh = makeConductionMesh();
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  ThermalSolverSettings settings;
  settings.linearSolver.maxIterations = 1;
  settings.linearSolver.absoluteTolerance = 1e-300;
  settings.linearSolver.relativeTolerance = 1e-300;
  const ThermalSolver solver{settings};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::LinearSolveFailure);
  EXPECT_FALSE(result.converged());
}

TEST(ThermalSolverTest, InvalidConfigurationForInvalidLinearSolverSettings) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  ThermalSolverSettings settings;
  settings.linearSolver.maxIterations = 0;  // rejected by LinearSolver's own constructor.
  const ThermalSolver solver{settings};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::InvalidConfiguration);
}

TEST(ThermalSolverTest, InvalidConfigurationForZeroOuterMaxIterations) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  ThermalSolverSettings settings;
  settings.maxIterations = 0;
  const ThermalSolver solver{settings};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::InvalidConfiguration);
}

TEST(ThermalSolverTest, InvalidConfigurationForNonPositiveTolerance) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  ThermalSolverSettings settings;
  settings.tolerance = 0.0;
  const ThermalSolver solver{settings};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::InvalidConfiguration);
}

TEST(ThermalSolverTest, NonFiniteStateForNonFiniteInitialTemperature) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  initialTemperature[0] = std::numeric_limits<Real>::quiet_NaN();
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  const ThermalSolver solver{};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::NonFiniteState);
}

TEST(ThermalSolverTest, NonFiniteStateForNonFiniteMassFlux) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[0] = std::numeric_limits<Real>::infinity();
  const ThermalProperties thermal(0.6, 4180.0);

  const ThermalSolver solver{};
  const ThermalResult result =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::NonFiniteState);
}

TEST(ThermalSolverTest, RepeatedSolveIsDeterministic) {
  const Mesh mesh = makeConductionMesh();
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const Index n = mesh.numberOfCells();
  const ScalarField initialTemperature(n, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ThermalProperties thermal(0.6, 4180.0);

  const ThermalSolver solver{};
  const ThermalResult a = solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  const ThermalResult b = solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);

  ASSERT_EQ(a.status, ThermalStatus::Converged);
  ASSERT_EQ(b.status, ThermalStatus::Converged);
  EXPECT_EQ(a.iterations, b.iterations);
  EXPECT_EQ(a.finalResidual, b.finalResidual);
  ASSERT_EQ(a.residualHistory.size(), b.residualHistory.size());
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.temperature[i], b.temperature[i]);
  }
  for (std::size_t i = 0; i < a.residualHistory.size(); ++i) {
    EXPECT_EQ(a.residualHistory[i], b.residualHistory[i]);
  }
}

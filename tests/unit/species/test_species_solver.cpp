// P3-PHYS-004: SpeciesSolver -- mirrors test_thermal_solver.cpp's own
// structure (config-error handling, determinism, a smoke-test analytical
// case). The primary analytical/conservation/coupling validations live
// under tests/integration/species/ (this task's own section 32).
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/species/SpeciesProperties.hpp"
#include "cfd/species/SpeciesSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::species::SpeciesProperties;
using cfd::species::SpeciesResult;
using cfd::species::SpeciesSolver;
using cfd::species::SpeciesSolverSettings;
using cfd::species::SpeciesStatus;

namespace {

// 1D pure-diffusion slab: left/right FixedValue, top/bottom zero-gradient
// (impermeable), no flow (massFlux == 0), no source -- same shape as
// ThermalSolverTest's own 1D-conduction smoke test. Analytical solution:
// Y(x) = Yleft + (Yright-Yleft)*x/L, exact for this FVM scheme on an
// orthogonal Cartesian mesh (affine field, zero second derivative).
Mesh makeSlabMesh() { return MeshGeometry::createCartesian2D(20, 4, 1.0, 0.2); }

BoundaryConditionSet makeSlabBoundaries(const Mesh& mesh, Real yLeft, Real yRight) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(yLeft));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(yRight));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

}  // namespace

TEST(SpeciesSolverTest, ConvergesAndMatchesAnalytical1DDiffusionProfile) {
  const Mesh mesh = makeSlabMesh();
  const Real yLeft = 1.0, yRight = 0.0;
  const Real length = 1.0;
  const auto boundaries = makeSlabBoundaries(mesh, yLeft, yRight);
  const Index n = mesh.numberOfCells();
  const ScalarField initialConcentration(n, 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-3);

  const SpeciesSolver solver{};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);

  ASSERT_EQ(result.status, SpeciesStatus::Converged);
  EXPECT_TRUE(result.converged());
  EXPECT_EQ(result.concentration.size(), n);

  Real maxAbsError = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real exact = yLeft + (yRight - yLeft) * (cell.centroid().x / length);
    const Real error = std::abs(result.concentration[cell.id()] - exact);
    maxAbsError = std::max(maxAbsError, error);
  }
  EXPECT_LT(maxAbsError, 1e-6);
}

TEST(SpeciesSolverTest, InvalidConfigurationForMismatchedConcentrationSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeSlabBoundaries(mesh, 1.0, 0.0);
  const ScalarField initialConcentration(mesh.numberOfCells() + 1, 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-3);

  const SpeciesSolver solver{};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);
  EXPECT_EQ(result.status, SpeciesStatus::InvalidConfiguration);
}

TEST(SpeciesSolverTest, InvalidConfigurationForMismatchedMassFluxSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeSlabBoundaries(mesh, 1.0, 0.0);
  const ScalarField initialConcentration(mesh.numberOfCells(), 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-3);

  const SpeciesSolver solver{};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);
  EXPECT_EQ(result.status, SpeciesStatus::InvalidConfiguration);
}

TEST(SpeciesSolverTest, MaxIterationsWhenOuterBudgetTooSmall) {
  const Mesh mesh = makeSlabMesh();
  const auto boundaries = makeSlabBoundaries(mesh, 1.0, 0.0);
  const ScalarField initialConcentration(mesh.numberOfCells(), 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-3);

  SpeciesSolverSettings settings;
  settings.maxIterations = 1;
  const SpeciesSolver solver{settings};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);
  EXPECT_EQ(result.status, SpeciesStatus::MaxIterations);
  EXPECT_EQ(result.iterations, 1u);
}

TEST(SpeciesSolverTest, LinearSolveFailureWhenInnerSolverIsStarved) {
  const Mesh mesh = makeSlabMesh();
  const auto boundaries = makeSlabBoundaries(mesh, 1.0, 0.0);
  const ScalarField initialConcentration(mesh.numberOfCells(), 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-3);

  SpeciesSolverSettings settings;
  settings.linearSolver.maxIterations = 1;
  settings.linearSolver.absoluteTolerance = 1e-300;
  settings.linearSolver.relativeTolerance = 1e-300;
  const SpeciesSolver solver{settings};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);
  EXPECT_EQ(result.status, SpeciesStatus::LinearSolveFailure);
}

TEST(SpeciesSolverTest, InvalidConfigurationForZeroOuterMaxIterations) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeSlabBoundaries(mesh, 1.0, 0.0);
  const ScalarField initialConcentration(mesh.numberOfCells(), 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-3);

  SpeciesSolverSettings settings;
  settings.maxIterations = 0;
  const SpeciesSolver solver{settings};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);
  EXPECT_EQ(result.status, SpeciesStatus::InvalidConfiguration);
}

TEST(SpeciesSolverTest, NonFiniteStateForNonFiniteInitialConcentration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeSlabBoundaries(mesh, 1.0, 0.0);
  ScalarField initialConcentration(mesh.numberOfCells(), 0.5);
  initialConcentration[0] = std::numeric_limits<Real>::quiet_NaN();
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-3);

  const SpeciesSolver solver{};
  const SpeciesResult result =
      solver.solve(mesh, initialConcentration, massFlux, fluid, species, boundaries);
  EXPECT_EQ(result.status, SpeciesStatus::NonFiniteState);
}

TEST(SpeciesSolverTest, RepeatedSolveIsDeterministic) {
  const Mesh mesh = makeSlabMesh();
  const auto boundaries = makeSlabBoundaries(mesh, 1.0, 0.0);
  const Index n = mesh.numberOfCells();
  const ScalarField initialConcentration(n, 0.5);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const FluidProperties fluid(1.0, 1.0);
  const SpeciesProperties species("tracer", 1.0e-3);

  const SpeciesSolver solver{};
  const SpeciesResult a = solver.solve(mesh, initialConcentration, massFlux, fluid, species,
                                       boundaries);
  const SpeciesResult b = solver.solve(mesh, initialConcentration, massFlux, fluid, species,
                                       boundaries);

  ASSERT_EQ(a.status, SpeciesStatus::Converged);
  ASSERT_EQ(b.status, SpeciesStatus::Converged);
  EXPECT_EQ(a.iterations, b.iterations);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.concentration[i], b.concentration[i]);
  }
}

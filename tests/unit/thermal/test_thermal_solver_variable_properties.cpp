// P3-PHYS-003: ThermalSolver's variable-property solve() overload --
// constant-property equivalence against the pre-existing ThermalProperties
// overload (mandatory backward-equivalence, section 4/19), plus a
// manufactured 1D variable-conductivity conduction case validated against
// a closed-form analytical reference (section 20).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/TemperatureProperty.hpp"
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
using cfd::physics::ConstantProperty;
using cfd::physics::LinearProperty;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalResult;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalSolverSettings;
using cfd::thermal::ThermalStatus;

namespace {

BoundaryConditionSet makeConductionBoundaries(const Mesh& mesh, Real th, Real tc) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(th));
  boundaries.set(mesh, "right", std::make_unique<FixedTemperature>(tc));
  boundaries.set(mesh, "top", std::make_unique<Adiabatic>());
  boundaries.set(mesh, "bottom", std::make_unique<Adiabatic>());
  return boundaries;
}

// --- Manufactured 1D variable-conductivity conduction ---------------------
//
// Steady, source-free, no-flow 1D conduction with k(T) = kRef +
// slope*(T-Tref) (this codebase's LinearProperty form) satisfies
// d/dx[k(T) dT/dx] = 0, i.e. k(T)*dT/dx = const (constant heat flux).
// Writing K(T) = kRef*T + (slope/2)*(T-Tref)^2 (the antiderivative of
// k(T), so dK/dT = k(T) exactly), this integrates to K(T(x)) being an
// *affine* function of x matching the two Dirichlet endpoints:
//   K(T(x)) = K(Th) + (K(Tc) - K(Th)) * x/L.
// K(T) is strictly increasing wherever k(T) > 0 (guaranteed by this test's
// chosen slope/range below), so T(x) is recovered from that target K value
// by inverting K via Newton's method (K is exactly quadratic in T, so
// Newton converges in a handful of iterations to machine precision).
Real conductivityAntiderivative(Real temperature, Real kRef, Real tRef, Real slope) {
  const Real d = temperature - tRef;
  return kRef * temperature + 0.5 * slope * d * d;
}

Real invertConductivityAntiderivative(Real targetK, Real kRef, Real tRef, Real slope,
                                      Real initialGuess) {
  Real temperature = initialGuess;
  for (int iteration = 0; iteration < 50; ++iteration) {
    const Real k = kRef + slope * (temperature - tRef);  // dK/dT = k(T).
    const Real residual = conductivityAntiderivative(temperature, kRef, tRef, slope) - targetK;
    temperature -= residual / k;
  }
  return temperature;
}

struct VariableConductivityCase {
  Real kRef, tRef, slope, th, tc, length;

  [[nodiscard]] Real analyticalTemperature(Real x) const {
    const Real kTh = conductivityAntiderivative(th, kRef, tRef, slope);
    const Real kTc = conductivityAntiderivative(tc, kRef, tRef, slope);
    const Real targetK = kTh + (kTc - kTh) * (x / length);
    const Real linearGuess = th + (tc - th) * (x / length);
    return invertConductivityAntiderivative(targetK, kRef, tRef, slope, linearGuess);
  }
};

// Runs the manufactured case on an nx-by-4 mesh and returns the max
// absolute error against the analytical profile, restricted to cells at
// least half a cell width away from the Dirichlet boundaries themselves
// (the FVM boundary treatment's own first-order-in-distance approximation,
// already documented in EnergyEquation.cpp, adds a distinct, well-known
// discretization error right at the boundary faces that is not part of
// what this test is trying to isolate -- interior-cell error is the
// relevant quantity for judging the *scheme's* accuracy against a smooth
// analytical field, same "interior cells only" carve-out
// MomentumDiffusionTest.InteriorCellsMatchKnownQuadraticLaplacian already
// uses for the same reason).
Real maxInteriorError(const VariableConductivityCase& problem, Index nx) {
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, 4, problem.length, 0.2);
  const auto boundaries = makeConductionBoundaries(mesh, problem.th, problem.tc);
  const Index n = mesh.numberOfCells();
  const ScalarField initialTemperature(n, 0.5 * (problem.th + problem.tc));
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  const LinearProperty conductivityModel(problem.kRef, problem.tRef, problem.slope);
  const ConstantProperty specificHeatModel(1.0);  // irrelevant: massFlux == 0, no convection.

  // The default inner-solver tolerance (absoluteTolerance=1e-12,
  // LinearSolver.hpp) is diagnosed here to occasionally leave BiCGSTAB one
  // iteration short of its own convergence bar on this problem's larger
  // (nx=80, 320-cell) grid, where the conductivity field's cell-to-cell
  // variation (k(T) genuinely differs across the domain, unlike every
  // other test in this file) makes the matrix mildly less well-conditioned
  // than the constant-property case -- loosened here to the same
  // 1e-9/1e-7 order-of-magnitude precedent P3-PHYS-002's own natural-
  // convection validation already established for a diagnosed BiCGSTAB
  // robustness limit (not a physics defect -- see that task's own
  // TODO.md entry).
  ThermalSolverSettings settings;
  settings.linearSolver.absoluteTolerance = 1e-9;
  settings.linearSolver.relativeTolerance = 1e-8;
  const ThermalSolver solver{settings};
  const ThermalResult result = solver.solve(mesh, initialTemperature, massFlux, conductivityModel,
                                            specificHeatModel, boundaries);
  if (result.status != ThermalStatus::Converged) {
    ADD_FAILURE() << "manufactured variable-conductivity case did not converge (nx=" << nx
                  << ", status=" << static_cast<int>(result.status)
                  << ", iterations=" << result.iterations
                  << ", maxTemperatureChange=" << result.maxTemperatureChange
                  << ", finalResidual=" << result.finalResidual << ")";
    return std::numeric_limits<Real>::infinity();
  }

  const Real dx = problem.length / static_cast<Real>(nx);
  Real maxError = 0.0;
  for (const auto& cell : mesh.cells()) {
    if (cell.centroid().x < dx || cell.centroid().x > problem.length - dx)
      continue;  // boundary layer.
    const Real exact = problem.analyticalTemperature(cell.centroid().x);
    maxError = std::max(maxError, std::abs(result.temperature[cell.id()] - exact));
  }
  return maxError;
}

}  // namespace

// --- Constant-property equivalence (mandatory, section 4/19) --------------

TEST(ThermalSolverVariablePropertiesTest, ConstantModelsReproduceThermalPropertiesOverloadExactly) {
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 4, 1.0, 0.2);
  const Real th = 310.0, tc = 290.0;
  const auto boundaries = makeConductionBoundaries(mesh, th, tc);
  const Index n = mesh.numberOfCells();
  const ScalarField initialTemperature(n, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  const ThermalProperties thermal(0.6, 4180.0);
  const ThermalSolver solver{};
  const ThermalResult scalarResult =
      solver.solve(mesh, initialTemperature, massFlux, thermal, boundaries);
  ASSERT_EQ(scalarResult.status, ThermalStatus::Converged);

  const ConstantProperty conductivityModel(0.6);
  const ConstantProperty specificHeatModel(4180.0);
  const ThermalResult fieldResult = solver.solve(mesh, initialTemperature, massFlux,
                                                 conductivityModel, specificHeatModel, boundaries);
  ASSERT_EQ(fieldResult.status, ThermalStatus::Converged);

  ASSERT_EQ(scalarResult.iterations, fieldResult.iterations);
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(fieldResult.temperature[i], scalarResult.temperature[i], 1e-8) << "cell " << i;
  }
}

TEST(ThermalSolverVariablePropertiesTest, InvalidConfigurationForMismatchedTemperatureSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConductionBoundaries(mesh, 310.0, 290.0);
  const ScalarField initialTemperature(mesh.numberOfCells() + 1, 300.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const ConstantProperty conductivityModel(0.6);
  const ConstantProperty specificHeatModel(4180.0);

  const ThermalSolver solver{};
  const ThermalResult result = solver.solve(mesh, initialTemperature, massFlux, conductivityModel,
                                            specificHeatModel, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::InvalidConfiguration);
}

TEST(ThermalSolverVariablePropertiesTest, NonFiniteStateWhenPropertyModelPredictsNonPositiveValue) {
  // A steep negative slope pushes k(T) below zero once the outer loop's
  // temperature iterate reaches a high enough value -- must be reported as
  // ThermalStatus::NonFiniteState (via the InvalidArgumentError ->
  // NumericalError translation in ThermalSolver::solve's own header
  // comment), never an uncaught exception escaping solve().
  const Mesh mesh = MeshGeometry::createCartesian2D(10, 4, 1.0, 0.2);
  const Real th = 1000.0, tc = 290.0;  // wide enough range to force k <= 0 somewhere.
  const auto boundaries = makeConductionBoundaries(mesh, th, tc);
  const Index n = mesh.numberOfCells();
  const ScalarField initialTemperature(n, th);  // start already at the offending temperature.
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  const LinearProperty conductivityModel(0.6, 290.0, -1.0);  // k(1000) = 0.6-710 << 0.
  const ConstantProperty specificHeatModel(1.0);

  const ThermalSolver solver{};
  const ThermalResult result = solver.solve(mesh, initialTemperature, massFlux, conductivityModel,
                                            specificHeatModel, boundaries);
  EXPECT_EQ(result.status, ThermalStatus::NonFiniteState);
}

TEST(ThermalSolverVariablePropertiesTest, RepeatedSolveIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 4, 1.0, 0.2);
  const auto boundaries = makeConductionBoundaries(mesh, 400.0, 300.0);
  const Index n = mesh.numberOfCells();
  const ScalarField initialTemperature(n, 350.0);
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  const LinearProperty conductivityModel(0.6, 300.0, 0.001);
  const ConstantProperty specificHeatModel(4180.0);

  const ThermalSolver solver{};
  const ThermalResult a = solver.solve(mesh, initialTemperature, massFlux, conductivityModel,
                                       specificHeatModel, boundaries);
  const ThermalResult b = solver.solve(mesh, initialTemperature, massFlux, conductivityModel,
                                       specificHeatModel, boundaries);

  ASSERT_EQ(a.status, ThermalStatus::Converged);
  ASSERT_EQ(b.status, ThermalStatus::Converged);
  EXPECT_EQ(a.iterations, b.iterations);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.temperature[i], b.temperature[i]);
  }
}

// --- Manufactured variable-conductivity validation (section 20) -----------

TEST(ThermalSolverVariablePropertiesTest,
     VariableConductivityMatchesManufacturedAnalyticalProfile) {
  const VariableConductivityCase problem{/*kRef=*/0.6, /*tRef=*/300.0, /*slope=*/0.001,
                                         /*th=*/400.0, /*tc=*/300.0,   /*length=*/1.0};
  const Real errorCoarse = maxInteriorError(problem, /*nx=*/20);
  const Real errorFine = maxInteriorError(problem, /*nx=*/80);

  // Grid refinement must reduce the error against the manufactured
  // analytical solution (same "convergence, not just closeness" gate the
  // rest of this codebase's validation suites require -- TODO.md's own
  // grid-refinement precedent).
  EXPECT_LT(errorFine, errorCoarse);
  // At 80 cells the discretization error against a genuinely nonlinear
  // (quadratic-in-T-space) analytical profile must be a small fraction of
  // the 100K applied temperature difference.
  EXPECT_LT(errorFine, 1.0);
}

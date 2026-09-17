// P12-NUM-004: CompressibleSIMPLE on the SAME shared robustness
// infrastructure as SIMPLE (cfd::solver::OuterIterationMonitor and the
// linear-solver fallback policy):
//   - RobustnessRegression: default settings bit-identical to the
//     pre-P12-NUM-004 solver (and detectors that do not fire change
//     nothing); the adaptive controller on the genuinely compressible air
//     cavity converges faster than the fixed default relaxation, with the
//     EOS density still iterated;
//   - FallbackRecovery: the P12-COMP-002 channel started from rest -- its
//     BiCGSTAB pressure solve used to break down falsely (a scale artefact
//     fixed in P12-MESH-004); it now runs to its budget and the fallback is
//     never needed;
//   - LowMachRegression: with adaptive relaxation and the normalized
//     criterion in BOTH solvers, a near-incompressible gas still reproduces
//     incompressible SIMPLE's converged solution (the iteration paths --
//     and so the relaxation decisions -- legitimately differ).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleSIMPLE.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::compressible::CompressibleSIMPLE;
using cfd::compressible::CompressibleSIMPLEResult;
using cfd::compressible::CompressibleSIMPLESettings;
using cfd::compressible::CompressibleSIMPLEStatus;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

BoundaryConditionSet cavityVelocity(const Mesh& mesh) {
  BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<cfd::boundary::Wall>());
  b.set(mesh, "right", std::make_unique<cfd::boundary::Wall>());
  b.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  b.set(mesh, "top", std::make_unique<cfd::boundary::MovingWall>(Vector2{1.0, 0.0}));
  return b;
}

BoundaryConditionSet zeroGradientPressure(const Mesh& mesh) {
  BoundaryConditionSet b;
  for (const auto& patch : mesh.boundaryPatches()) {
    b.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return b;
}

CompressibleSIMPLESettings cavitySettings(Real alphaU, Real alphaP) {
  CompressibleSIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityRelaxation = alphaU;
  s.pressureRelaxation = alphaP;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  return s;
}

// Air (R = 287.05, cp = 1005) at 300 K, p_ref = 101325, mu = 1.8e-5 in an
// 8x8 unit cavity with a 1 m/s lid.
CompressibleSIMPLEResult solveAirCavity(const CompressibleSIMPLESettings& settings) {
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const ThermodynamicProperties air(287.05, 1005.0);
  const Index n = mesh.numberOfCells();
  const CompressibleSIMPLE solver(settings, air, 101325.0, 0);
  return solver.solve(mesh, 1.8e-5, cavityVelocity(mesh), zeroGradientPressure(mesh),
                      ScalarField(n, 300.0), nullptr, VectorField(n, Vector2{0.0, 0.0}),
                      ScalarField(n, 0.0), ScalarField(n, air.density(101325.0, 300.0)));
}

void expectBitIdentical(const CompressibleSIMPLEResult& a, const CompressibleSIMPLEResult& b) {
  ASSERT_EQ(a.status, b.status);
  ASSERT_EQ(a.iterations, b.iterations);
  for (Index k = 0; k < a.uResidualHistory.size(); ++k) {
    EXPECT_EQ(a.uResidualHistory[k], b.uResidualHistory[k]);
    EXPECT_EQ(a.pressureResidualHistory[k], b.pressureResidualHistory[k]);
  }
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y);
    EXPECT_EQ(a.pressure[i], b.pressure[i]);
    EXPECT_EQ(a.density[i], b.density[i]);
  }
}

}  // namespace

TEST(CompressibleSIMPLERobustnessTest, RobustnessRegression) {
  const CompressibleSIMPLESettings base = cavitySettings(0.7, 0.3);
  const CompressibleSIMPLEResult reference = solveAirCavity(base);
  ASSERT_EQ(reference.status, CompressibleSIMPLEStatus::Converged);

  CompressibleSIMPLESettings guarded = base;
  guarded.robustness.stagnation.enabled = true;
  guarded.robustness.divergence.enabled = true;
  guarded.robustness.linearSolverFallback.enabled = true;
  expectBitIdentical(reference, solveAirCavity(guarded));

  CompressibleSIMPLESettings adaptive = base;
  adaptive.robustness.adaptiveRelaxation.enabled = true;
  adaptive.robustness.adaptiveRelaxation.minVelocity = 0.2;
  adaptive.robustness.adaptiveRelaxation.maxVelocity = 0.95;
  adaptive.robustness.adaptiveRelaxation.minPressure = 0.05;
  adaptive.robustness.adaptiveRelaxation.maxPressure = 0.95;
  const CompressibleSIMPLEResult tuned = solveAirCavity(adaptive);
  std::printf(
      "\nCompressible air cavity 8x8: fixed 0.7/0.3 %llu iterations; adaptive %llu iterations"
      " (alpha %.4g/%.4g at the end, %llu increases, %llu decreases); max |rho - rho_0| %.3g\n",
      static_cast<unsigned long long>(reference.iterations),
      static_cast<unsigned long long>(tuned.iterations),
      tuned.robustness.velocityRelaxationHistory.back(),
      tuned.robustness.pressureRelaxationHistory.back(),
      static_cast<unsigned long long>(tuned.robustness.relaxationIncreases),
      static_cast<unsigned long long>(tuned.robustness.relaxationDecreases), [&] {
        Real worst = 0.0;
        for (Index i = 0; i < tuned.density.size(); ++i) {
          worst = std::max(worst, std::abs(tuned.density[i] - tuned.density[0]));
        }
        return worst;
      }());
  ASSERT_EQ(tuned.status, CompressibleSIMPLEStatus::Converged);
  EXPECT_LT(tuned.iterations, reference.iterations);
  EXPECT_LE(tuned.finalContinuityResidual, 1e-6);
  for (Index i = 0; i < tuned.density.size(); ++i) {
    EXPECT_TRUE(std::isfinite(tuned.density[i]));
    EXPECT_GT(tuned.density[i], 0.0);
  }
  // Same SOLUTION, not just the same tolerance: at a 1e-6 residual
  // tolerance the two stopping points differ at the 1e-4 level (each is
  // ~2-3e-4 from the fully converged field on this high-Re cavity --
  // measured, see the evidence), so compare both relaxation strategies
  // driven to 1e-11 instead.
  CompressibleSIMPLESettings tightFixed = base;
  tightFixed.velocityTolerance = 1e-11;
  tightFixed.pressureTolerance = 1e-11;
  tightFixed.continuityTolerance = 1e-11;
  tightFixed.maxIterations = 20000;
  CompressibleSIMPLESettings tightAdaptive = tightFixed;
  tightAdaptive.robustness = adaptive.robustness;
  const CompressibleSIMPLEResult fixedTight = solveAirCavity(tightFixed);
  const CompressibleSIMPLEResult adaptiveTight = solveAirCavity(tightAdaptive);
  ASSERT_EQ(fixedTight.status, CompressibleSIMPLEStatus::Converged);
  ASSERT_EQ(adaptiveTight.status, CompressibleSIMPLEStatus::Converged);
  Real difference = 0.0;
  for (Index i = 0; i < fixedTight.velocity.size(); ++i) {
    difference =
        std::max(difference, cfd::magnitude(fixedTight.velocity[i] - adaptiveTight.velocity[i]));
  }
  std::printf(
      "Converged to 1e-11: fixed %llu iterations, adaptive %llu iterations, max |u_fixed -"
      " u_adaptive| %.3g\n",
      static_cast<unsigned long long>(fixedTight.iterations),
      static_cast<unsigned long long>(adaptiveTight.iterations), difference);
  EXPECT_LT(difference, 1e-8);
}

// The P12-COMP-002 channel (48x8, 1.0 x 0.05, inlet 20 m/s, fixed-pressure
// outlet, isothermal air at 300 K, p_ref = 101325) started from rest with the
// BiCGSTAB pressure solver. Before P12-MESH-004 its pressure solve reported
// Breakdown in outer iteration 64 and the fallback (CG) recovered it; that
// was the scale-dependent false breakdown P12-MESH-004 fixed (t . t =
// 9.0e-31 < 1e-30 only because |t| / |s| = 5.3e-8 times |s| = 1.8e-8 --
// results/p12-mesh-004/solver-robustness/05). Now the solve never breaks
// down: it runs to its budget with no linear failure, and enabling the
// fallback changes nothing. The fallback policy on a GENUINE breakdown is
// covered by LinearFallbackTest (tests/unit/algebra).
TEST(CompressibleSIMPLERobustnessTest, FallbackRecovery) {
  const Mesh mesh = MeshGeometry::createCartesian2D(48, 8, 1.0, 0.05);
  BoundaryConditionSet velocity;
  velocity.set(mesh, "left", std::make_unique<cfd::boundary::Inlet>(Vector2{20.0, 0.0}));
  velocity.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  velocity.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  velocity.set(mesh, "top", std::make_unique<cfd::boundary::Wall>());
  BoundaryConditionSet pressure;
  pressure.set(mesh, "left", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  pressure.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
  pressure.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  pressure.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  const ThermodynamicProperties air(287.05, 1005.0);
  const Index n = mesh.numberOfCells();

  CompressibleSIMPLESettings settings;
  settings.maxIterations = 80;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 2e-5;
  settings.pressureTolerance = 5e-4;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.momentumSolver.maxIterations = 500;
  settings.pressureSolver.type = cfd::algebra::LinearSolverType::BiCGSTAB;
  settings.pressureSolver.absoluteTolerance = 1e-8;
  settings.pressureSolver.relativeTolerance = 1e-6;
  settings.pressureSolver.maxIterations = 2000;
  const auto run = [&](const CompressibleSIMPLESettings& s) {
    const CompressibleSIMPLE solver(s, air, 101325.0, 0);
    return solver.solve(mesh, 0.58831, velocity, pressure, ScalarField(n, 300.0), nullptr,
                        VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0),
                        ScalarField(n, air.density(101325.0, 300.0)));
  };
  const CompressibleSIMPLEResult without = run(settings);
  settings.robustness.linearSolverFallback.enabled = true;
  const CompressibleSIMPLEResult with = run(settings);
  std::printf(
      "\nP12-COMP-002 channel, CompressibleSIMPLE from rest, BiCGSTAB pressure: fallback off -> "
      "status %d after %llu iterations (%s); fallback on -> status %d after %llu iterations,"
      " %llu fallback(s), %llu recovered\n",
      static_cast<int>(without.status), static_cast<unsigned long long>(without.iterations),
      without.robustness.statusDetail.c_str(), static_cast<int>(with.status),
      static_cast<unsigned long long>(with.iterations),
      static_cast<unsigned long long>(with.robustness.linearSolverFallbacks),
      static_cast<unsigned long long>(with.robustness.linearSolverFallbackRecoveries));

  EXPECT_EQ(without.status, CompressibleSIMPLEStatus::MaxIterations);  // ran to the budget
  EXPECT_EQ(without.iterations, 80u);
  EXPECT_TRUE(without.robustness.statusDetail.empty());  // no linear-solver failure
  EXPECT_EQ(with.robustness.linearSolverFallbacks, 0u);
  EXPECT_TRUE(with.robustness.fallbackEvents.empty());
  expectBitIdentical(without, with);
  for (Index i = 0; i < n; ++i) {
    EXPECT_TRUE(std::isfinite(without.density[i]));
    EXPECT_GT(without.density[i], 0.0);
  }
}

// P12-COMP-002's low-Mach reduction gate with the new robustness features
// active in both solvers: a near-incompressible gas (R = 1e10) and plain
// SIMPLE, both with adaptive relaxation + normalized criterion, make the
// same relaxation decisions and reach the same solution.
TEST(CompressibleSIMPLERobustnessTest, LowMachRegression) {
  const Mesh mesh = MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const Real rho = 1.0;
  const Real mu = 0.01;
  cfd::solver::SolverRobustnessSettings robustness;
  robustness.convergenceCriterion = cfd::solver::ConvergenceCriterion::Normalized;
  robustness.normalization.velocityTolerance = 1e-6;
  robustness.normalization.pressureTolerance = 1e-6;
  robustness.adaptiveRelaxation.enabled = true;
  robustness.adaptiveRelaxation.maxVelocity = 0.9;
  robustness.adaptiveRelaxation.maxPressure = 0.6;

  cfd::pressure_velocity::SIMPLESettings incompressible;
  incompressible.maxIterations = 3000;
  incompressible.velocityTolerance = 1e-10;
  incompressible.pressureTolerance = 1e-10;
  incompressible.continuityTolerance = 1e-8;
  incompressible.momentumSolver.absoluteTolerance = 1e-13;
  incompressible.momentumSolver.relativeTolerance = 1e-11;
  incompressible.pressureSolver.absoluteTolerance = 1e-13;
  incompressible.pressureSolver.relativeTolerance = 1e-11;
  incompressible.pressureSolver.maxIterations = 2000;
  incompressible.robustness = robustness;
  const cfd::pressure_velocity::SIMPLE simple(incompressible, 0);
  const auto reference = simple.solve(mesh, cfd::physics::FluidProperties(rho, mu),
                                      cavityVelocity(mesh), zeroGradientPressure(mesh),
                                      VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0));
  ASSERT_EQ(reference.status, cfd::pressure_velocity::SIMPLEStatus::Converged);

  const Real gasConstant = 1.0e10;
  const Real temperature = 300.0;
  const ThermodynamicProperties gas(gasConstant, 2.0e10);
  CompressibleSIMPLESettings settings;
  settings.maxIterations = incompressible.maxIterations;
  settings.velocityTolerance = incompressible.velocityTolerance;
  settings.pressureTolerance = incompressible.pressureTolerance;
  settings.continuityTolerance = incompressible.continuityTolerance;
  settings.momentumSolver = incompressible.momentumSolver;
  settings.pressureSolver = incompressible.pressureSolver;
  settings.robustness = robustness;
  const CompressibleSIMPLE compressible(settings, gas, rho * gasConstant * temperature, 0);
  const auto result = compressible.solve(
      mesh, mu, cavityVelocity(mesh), zeroGradientPressure(mesh), ScalarField(n, temperature),
      nullptr, VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0), ScalarField(n, rho));
  ASSERT_EQ(result.status, CompressibleSIMPLEStatus::Converged);

  Real velocityDifference = 0.0;
  for (Index i = 0; i < n; ++i) {
    velocityDifference =
        std::max({velocityDifference, std::abs(result.velocity[i].x - reference.velocity[i].x),
                  std::abs(result.velocity[i].y - reference.velocity[i].y)});
    EXPECT_NEAR(result.density[i], rho, 1e-6);
  }
  const auto& a = reference.robustness.velocityRelaxationHistory;
  const auto& b = result.robustness.velocityRelaxationHistory;
  const std::size_t common = std::min(a.size(), b.size());
  Index sameDecisions = 0;
  for (std::size_t k = 0; k < common && a[k] == b[k]; ++k) ++sameDecisions;
  std::printf(
      "\nLow-Mach limit with adaptive relaxation + normalized criterion: max |u_c - u_SIMPLE| %.3g;"
      " iterations SIMPLE %llu / compressible %llu; identical relaxation history for the first"
      " %llu iterations (of %zu); decreases %llu / %llu, increases %llu / %llu\n",
      velocityDifference, static_cast<unsigned long long>(reference.iterations),
      static_cast<unsigned long long>(result.iterations),
      static_cast<unsigned long long>(sameDecisions), common,
      static_cast<unsigned long long>(reference.robustness.relaxationDecreases),
      static_cast<unsigned long long>(result.robustness.relaxationDecreases),
      static_cast<unsigned long long>(reference.robustness.relaxationIncreases),
      static_cast<unsigned long long>(result.robustness.relaxationIncreases));
  // The low-Mach gate of P12-COMP-002 / P12-NUM-003 (1e-4). The iteration
  // PATHS legitimately differ -- CompressibleSIMPLE's momentum carries a
  // pseudo-transient term plain SIMPLE does not (different iteration counts
  // even with fixed relaxation, cf. CompressibleSIMPLENonOrthogonalTest.
  // LowMachRegression) -- so the adaptive decisions are reported, not
  // required to coincide; the converged solutions must.
  EXPECT_LT(velocityDifference, 1e-4);
}

// P12-NUM-003: the non-orthogonal correction loop
// (SIMPLESettings::nonOrthogonalCorrections, solver.json
// "non_orthogonal_corrections") through the production momentum path.
//
// Covered here:
//   - full SIMPLE solves on Cartesian meshes: 0 is the pre-P12-NUM-003
//     solver bit-for-bit, and N = 1 is ALSO bit-identical (the correction
//     is exactly zero there), extra passes differ only by linear-solver
//     tolerance;
//   - the momentum correction-loop machinery (runNonOrthogonalCorrectionPasses,
//     the single implementation SIMPLE calls) on distorted meshes: passes
//     execute, the corrector iteration converges, its fixed point satisfies
//     the fully corrected momentum equation, and it is deterministic;
//   - (P12-NUM-003 continuation) FULL SIMPLE on distorted meshes: the
//     pressure-correction equation is now geometric, so the whole solver
//     runs, converges, conserves mass, runs exactly N passes, and -- on an
//     exact Couette solution -- is more accurate with the correction.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/algebra/LinearSolverFactory.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/RelaxedMomentum.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::ConvectionScheme;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::physics::MomentumAssembly;
using cfd::physics::VelocityComponent;
using cfd::pressure_velocity::assembleRelaxedMomentumComponent;
using cfd::pressure_velocity::NonOrthogonalPassResult;
using cfd::pressure_velocity::NonOrthogonalPassStatus;
using cfd::pressure_velocity::runNonOrthogonalCorrectionPasses;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

BoundaryConditionSet cavityVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::MovingWall>(Vector2{1.0, 0.0}));
  return boundaries;
}

BoundaryConditionSet zeroGradientPressure(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return boundaries;
}

// Same settings as SIMPLEDeterminismTest's cavity (known to converge).
SIMPLESettings cavitySettings() {
  SIMPLESettings settings;
  settings.maxIterations = 500;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  return settings;
}

SIMPLEResult solveCavity(const Mesh& mesh, const SIMPLESettings& settings) {
  const FluidProperties fluid(1.0, 0.01);
  const auto velocityBoundaries = cavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = zeroGradientPressure(mesh);
  const SIMPLE simple(settings, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  return simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, initialVelocity,
                      initialPressure);
}

void expectBitIdentical(const SIMPLEResult& a, const SIMPLEResult& b) {
  ASSERT_EQ(a.status, b.status);
  ASSERT_EQ(a.iterations, b.iterations);
  ASSERT_EQ(a.uResidualHistory.size(), b.uResidualHistory.size());
  for (Index i = 0; i < a.uResidualHistory.size(); ++i) {
    EXPECT_EQ(a.uResidualHistory[i], b.uResidualHistory[i]);
    EXPECT_EQ(a.vResidualHistory[i], b.vResidualHistory[i]);
    EXPECT_EQ(a.pressureResidualHistory[i], b.pressureResidualHistory[i]);
    EXPECT_EQ(a.continuityHistory[i], b.continuityHistory[i]);
  }
  ASSERT_EQ(a.velocity.size(), b.velocity.size());
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y);
    EXPECT_EQ(a.pressure[i], b.pressure[i]);
  }
  for (Index i = 0; i < a.massFlux.size(); ++i) {
    EXPECT_EQ(a.massFlux[i], b.massFlux[i]);
  }
}

Real maxVelocityDifference(const VectorField& a, const VectorField& b) {
  Real worst = 0.0;
  for (Index i = 0; i < a.size(); ++i) {
    worst = std::max(worst, cfd::magnitude(a[i] - b[i]));
  }
  return worst;
}

// One momentum-predictor setup on a DISTORTED mesh (the piece of SIMPLE
// that is geometry-generic): a lid-driven-cavity-like state with a
// nontrivial lagged velocity, its mass flux, and the pass-1 corrected
// predictor solve -- the exact inputs SIMPLE hands to
// runNonOrthogonalCorrectionPasses.
struct PredictorFixture {
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  FluidProperties fluid{1.0, 0.05};
  VectorField velocity;
  ScalarField pressure;
  SurfaceField massFlux;
  ScalarField mu;
  ScalarField previousU;
  ScalarField previousV;
  Real alpha = 0.7;
  std::unique_ptr<cfd::algebra::LinearSolver> solver;
  VectorField velocityStar;

  explicit PredictorFixture(Index n, Real amplitudeFraction)
      : mesh(cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0,
                                              amplitudeFraction / static_cast<Real>(n))) {
    velocityBoundaries = cavityVelocityBoundaries(mesh);
    pressureBoundaries = zeroGradientPressure(mesh);
    velocity = VectorField(mesh.numberOfCells());
    pressure = ScalarField(mesh.numberOfCells());
    for (const auto& cell : mesh.cells()) {
      const Vector2 c = cell.centroid();
      velocity[cell.id()] = Vector2{c.y * c.y * std::sin(cfd::constants::pi * c.x),
                                    -0.3 * std::sin(cfd::constants::pi * c.y) * c.x};
      pressure[cell.id()] = 0.1 * c.x * c.y;
    }
    massFlux = cfd::physics::calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);
    mu = ScalarField(mesh.numberOfCells(), fluid.dynamicViscosity());
    previousU = ScalarField(mesh.numberOfCells());
    previousV = ScalarField(mesh.numberOfCells());
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      previousU[i] = velocity[i].x;
      previousV[i] = velocity[i].y;
    }
    solver = cfd::algebra::makeLinearSolver(SIMPLESettings{}.momentumSolver);

    const MomentumAssembly u = assemble(VelocityComponent::U, nullptr);
    const MomentumAssembly v = assemble(VelocityComponent::V, nullptr);
    const auto uSolve = solver->solve(u.system, toVector(previousU));
    const auto vSolve = solver->solve(v.system, toVector(previousV));
    velocityStar = VectorField(mesh.numberOfCells());
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      velocityStar[i] = Vector2{uSolve.solution[i], vSolve.solution[i]};
    }
  }

  static Vector toVector(const ScalarField& field) {
    Vector v(field.size());
    for (Index i = 0; i < field.size(); ++i) v[i] = field[i];
    return v;
  }

  MomentumAssembly assemble(VelocityComponent component,
                            const VectorField* correctionVelocity) const {
    return assembleRelaxedMomentumComponent(
        mesh, velocity, pressure, massFlux, mu, velocityBoundaries, pressureBoundaries, component,
        (component == VelocityComponent::U) ? previousU : previousV, alpha, nullptr, nullptr,
        ConvectionScheme::Upwind, GradientScheme::LeastSquares, true, correctionVelocity);
  }

  NonOrthogonalPassResult run(Index totalPasses) const {
    return runNonOrthogonalCorrectionPasses(
        totalPasses, velocityStar, *solver, mesh, velocity, pressure, massFlux, mu,
        velocityBoundaries, pressureBoundaries, previousU, previousV, alpha, nullptr, nullptr,
        ConvectionScheme::Upwind, GradientScheme::LeastSquares);
  }

  // max_i |(A u* - b)_i| of the FULLY corrected equation, i.e. with the
  // explicit correction evaluated at the candidate velocity itself -- zero
  // exactly at the corrector iteration's fixed point.
  Real selfConsistentResidual(const VectorField& candidate) const {
    Real worst = 0.0;
    for (const VelocityComponent component : {VelocityComponent::U, VelocityComponent::V}) {
      const MomentumAssembly a = assemble(component, &candidate);
      Vector x(candidate.size());
      for (Index i = 0; i < candidate.size(); ++i) {
        x[i] = (component == VelocityComponent::U) ? candidate[i].x : candidate[i].y;
      }
      const Vector ax = a.system.matrix().multiply(x);
      for (Index i = 0; i < x.size(); ++i) {
        worst = std::max(worst, std::abs(ax[i] - a.system.rhs()[i]));
      }
    }
    return worst;
  }
};

}  // namespace

TEST(SIMPLENonOrthogonalTest, DefaultSettingsDisableTheCorrection) {
  EXPECT_EQ(SIMPLESettings{}.nonOrthogonalCorrections, 0u);
}

// Extra passes on a Cartesian mesh re-solve an identical system (the
// correction is exactly zero), so they can only differ from N = 0 by
// linear-solver tolerance.
TEST(SIMPLENonOrthogonalTest, CartesianExtraPassesOnlyDifferByLinearSolverTolerance) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  SIMPLESettings three = cavitySettings();
  three.nonOrthogonalCorrections = 3;
  const SIMPLEResult a = solveCavity(mesh, cavitySettings());
  const SIMPLEResult b = solveCavity(mesh, three);
  ASSERT_EQ(a.status, SIMPLEStatus::Converged);
  ASSERT_EQ(b.status, SIMPLEStatus::Converged);
  const Real difference = maxVelocityDifference(a.velocity, b.velocity);
  std::printf("\nCartesian cavity 4x4: N=0 %llu iterations, N=3 %llu iterations, max|du| %.3g\n",
              static_cast<unsigned long long>(a.iterations),
              static_cast<unsigned long long>(b.iterations), difference);
  EXPECT_LT(difference, 1e-6);
}

TEST(NonOrthogonalCorrectionPassesTest, AtMostOnePassRunsNothing) {
  const PredictorFixture fixture(8, 0.4);
  for (const Index total : {0u, 1u}) {
    const NonOrthogonalPassResult result = fixture.run(total);
    EXPECT_EQ(result.status, NonOrthogonalPassStatus::Completed);
    EXPECT_EQ(result.passesExecuted, 0u);
    EXPECT_FALSE(result.u.has_value());
    EXPECT_FALSE(result.v.has_value());
    EXPECT_TRUE(result.passIncrements.empty());
    EXPECT_EQ(maxVelocityDifference(result.velocityStar, fixture.velocityStar), 0.0);
  }
}

// N passes genuinely execute (N - 1 beyond pass 1), each changes the
// predictor velocity, and the corrector iteration CONVERGES: successive
// increments shrink monotonically (a contraction), and the final velocity
// satisfies the fully corrected momentum equation far better than the
// pass-1 velocity does. Measured values printed as evidence.
TEST(NonOrthogonalCorrectionPassesTest, PassesExecuteAndTheCorrectorIterationConverges) {
  const PredictorFixture fixture(12, 0.4);
  const NonOrthogonalPassResult result = fixture.run(8);
  ASSERT_EQ(result.status, NonOrthogonalPassStatus::Completed);
  EXPECT_EQ(result.passesExecuted, 7u);
  ASSERT_EQ(result.passIncrements.size(), 7u);
  ASSERT_TRUE(result.u.has_value());
  ASSERT_TRUE(result.v.has_value());

  std::printf("\nNon-orthogonal corrector passes, distorted 12x12 (0.4h): increments");
  for (const Real increment : result.passIncrements) {
    std::printf(" %.3g", increment);
  }
  const Real residualPass1 = fixture.selfConsistentResidual(fixture.velocityStar);
  const Real residualFinal = fixture.selfConsistentResidual(result.velocityStar);
  std::printf("\n  fully-corrected residual max|Au-b|: after pass 1 %.3g, after pass 8 %.3g\n",
              residualPass1, residualFinal);

  EXPECT_GT(result.passIncrements.front(), 1e-8) << "pass 2 must actually change the velocity";
  for (std::size_t k = 1; k < result.passIncrements.size(); ++k) {
    EXPECT_LT(result.passIncrements[k], result.passIncrements[k - 1]) << "pass " << (k + 2);
  }
  EXPECT_LT(result.passIncrements.back(), 1e-3 * result.passIncrements.front());
  EXPECT_LT(residualFinal, 1e-3 * residualPass1);
}

// The matrix handed back (used by SIMPLE for d = V/aP) is the same
// implicit operator as pass 1's: only the RHS changes between passes.
TEST(NonOrthogonalCorrectionPassesTest, PassesChangeOnlyTheRightHandSide) {
  const PredictorFixture fixture(8, 0.4);
  const NonOrthogonalPassResult result = fixture.run(3);
  ASSERT_EQ(result.status, NonOrthogonalPassStatus::Completed);
  const MomentumAssembly pass1 = fixture.assemble(VelocityComponent::U, nullptr);
  const auto& finalU = *result.u;
  ASSERT_EQ(pass1.system.matrix().nonZeros(), finalU.system.matrix().nonZeros());
  for (Index k = 0; k < pass1.system.matrix().nonZeros(); ++k) {
    EXPECT_EQ(pass1.system.matrix().valuesData()[k], finalU.system.matrix().valuesData()[k]);
  }
  for (Index i = 0; i < pass1.diagonal.size(); ++i) {
    EXPECT_EQ(pass1.diagonal[i], finalU.diagonal[i]);
  }
}

TEST(NonOrthogonalCorrectionPassesTest, Deterministic) {
  const PredictorFixture fixtureA(10, 0.4);
  const PredictorFixture fixtureB(10, 0.4);
  const NonOrthogonalPassResult a = fixtureA.run(4);
  const NonOrthogonalPassResult b = fixtureB.run(4);
  ASSERT_EQ(a.passIncrements.size(), b.passIncrements.size());
  for (std::size_t k = 0; k < a.passIncrements.size(); ++k) {
    EXPECT_EQ(a.passIncrements[k], b.passIncrements[k]);
  }
  for (Index i = 0; i < a.velocityStar.size(); ++i) {
    EXPECT_EQ(a.velocityStar[i].x, b.velocityStar[i].x);
    EXPECT_EQ(a.velocityStar[i].y, b.velocityStar[i].y);
  }
}

// ===========================================================================
// P12-NUM-003 continuation: SIMPLE on DISTORTED meshes, end to end. The
// pressure-correction equation is geometric (no axis-aligned-face
// restriction), so the full solver -- predictor, pressure correction,
// velocity/flux correction, continuity -- now runs on the non-orthogonal
// verification meshes.
// ===========================================================================

namespace {

// Distorted lid-driven cavity at amplitude `fraction` * h on an n x n mesh,
// solved to the cavity tolerance with the given correction count / scheme.
SIMPLEResult solveDistortedCavity(Index n, Real fraction, Index corrections,
                                  GradientScheme scheme) {
  const Mesh mesh =
      cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, fraction / static_cast<Real>(n));
  SIMPLESettings settings = cavitySettings();
  settings.maxIterations = 3000;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  settings.nonOrthogonalCorrections = corrections;
  settings.gradientScheme = scheme;
  return solveCavity(mesh, settings);
}

// Converged, finite, continuity closed per cell and globally, residual
// history genuinely decreased, pass counters exact.
void expectConvergedConservativeSolve(const SIMPLEResult& result, Index corrections,
                                      const char* label) {
  ASSERT_EQ(result.status, SIMPLEStatus::Converged) << label;
  for (Index i = 0; i < result.velocity.size(); ++i) {
    ASSERT_TRUE(std::isfinite(result.velocity[i].x) && std::isfinite(result.velocity[i].y))
        << label;
    ASSERT_TRUE(std::isfinite(result.pressure[i])) << label;
  }
  EXPECT_LE(result.finalContinuityResidual, 1e-6) << label;
  EXPECT_LE(result.globalMassImbalance, 1e-6) << label;
  // "Continuity decreases": the corrected flux satisfies continuity to
  // solver precision EVERY iteration (so its history is flat and tiny); the
  // quantity that genuinely decreases is the predictor mass imbalance each
  // pressure correction removes -- pressureResidualHistory (= ||b_p||).
  ASSERT_GE(result.pressureResidualHistory.size(), 2u) << label;
  EXPECT_LT(result.pressureResidualHistory.back(), 1e-3 * result.pressureResidualHistory.front())
      << label;
  for (const Real continuity : result.continuityHistory) {
    EXPECT_LE(continuity, 1e-9) << label;
  }
  const Index perIteration = std::max<Index>(1, corrections);
  EXPECT_EQ(result.pressureCorrectionPasses, result.iterations * perIteration) << label;
  EXPECT_EQ(result.momentumPredictorPasses, result.iterations * perIteration) << label;
  Real maxContinuity = 0.0;
  for (const Real continuity : result.continuityHistory)
    maxContinuity = std::max(maxContinuity, continuity);
  std::printf(
      "\nDistorted cavity 10x10 %s N=%llu: %llu iterations, final continuity %.3g, global"
      " imbalance %.3g, max continuity over history %.3g, ||b_p|| %.3g -> %.3g, passes p/u"
      " %llu/%llu\n",
      label, static_cast<unsigned long long>(corrections),
      static_cast<unsigned long long>(result.iterations), result.finalContinuityResidual,
      result.globalMassImbalance, maxContinuity, result.pressureResidualHistory.front(),
      result.pressureResidualHistory.back(),
      static_cast<unsigned long long>(result.pressureCorrectionPasses),
      static_cast<unsigned long long>(result.momentumPredictorPasses));
}

}  // namespace

// NonOrthogonalCorrectionsZeroPreservesBaseline, in its strongest form: an
// explicit 0 is the pre-P12-NUM-003 solver (identical code path), and on a
// Cartesian mesh even N = 1 (correction ON) is bit-identical -- every face
// decomposition is exactly {Sf, 0} -- for either gradient scheme. On a
// DISTORTED mesh, 0 means OFF: one predictor and one pressure pass per
// iteration, and the result genuinely differs from N = 1 (the correction
// does not leak into the uncorrected path). The Cartesian equivalence with
// the pre-continuation library itself (all production paths, bit for bit)
// is recorded in results/p12-num-003/summary.md.
TEST(SIMPLENonOrthogonalTest, NonOrthogonalCorrectionsZeroPreservesBaseline) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const SIMPLEResult baseline = solveCavity(mesh, cavitySettings());
  ASSERT_EQ(baseline.status, SIMPLEStatus::Converged);

  SIMPLESettings explicitZero = cavitySettings();
  explicitZero.nonOrthogonalCorrections = 0;
  expectBitIdentical(baseline, solveCavity(mesh, explicitZero));

  for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
    SIMPLESettings zero = cavitySettings();
    zero.gradientScheme = scheme;
    SIMPLESettings one = zero;
    one.nonOrthogonalCorrections = 1;
    expectBitIdentical(solveCavity(mesh, zero), solveCavity(mesh, one));
  }

  const SIMPLEResult uncorrected = solveDistortedCavity(8, 0.45, 0, GradientScheme::LeastSquares);
  const SIMPLEResult corrected = solveDistortedCavity(8, 0.45, 1, GradientScheme::LeastSquares);
  ASSERT_EQ(uncorrected.status, SIMPLEStatus::Converged);
  ASSERT_EQ(corrected.status, SIMPLEStatus::Converged);
  EXPECT_EQ(uncorrected.momentumPredictorPasses, uncorrected.iterations);
  EXPECT_EQ(uncorrected.pressureCorrectionPasses, uncorrected.iterations);
  const Real difference = maxVelocityDifference(uncorrected.velocity, corrected.velocity);
  std::printf("\nDistorted cavity 8x8 0.45h: max|u(N=0) - u(N=1)| = %.3g\n", difference);
  // Both converged to 1e-6; a difference far above that is the correction's
  // genuine effect, not solver noise.
  EXPECT_GT(difference, 1e-4);
}

TEST(SIMPLENonOrthogonalTest, DistortedMeshMild) {
  for (const Index corrections : {0u, 1u, 2u}) {
    expectConvergedConservativeSolve(
        solveDistortedCavity(10, 0.10, corrections, GradientScheme::LeastSquares), corrections,
        "mild 0.10h LS");
  }
}

TEST(SIMPLENonOrthogonalTest, DistortedMeshModerate) {
  for (const Index corrections : {0u, 1u, 2u}) {
    expectConvergedConservativeSolve(
        solveDistortedCavity(10, 0.25, corrections, GradientScheme::LeastSquares), corrections,
        "moderate 0.25h LS");
  }
}

TEST(SIMPLENonOrthogonalTest, DistortedMeshStrong) {
  for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
    for (const Index corrections : {0u, 1u, 2u}) {
      expectConvergedConservativeSolve(
          solveDistortedCavity(10, 0.45, corrections, scheme), corrections,
          scheme == GradientScheme::GreenGauss ? "strong 0.45h GG" : "strong 0.45h LS");
    }
  }
}

// Mass conservation of the converged flux field itself (not just the
// reported residuals): every cell's net outflow ~ 0, every wall face's flux
// ~ 0 (impermeable walls, including the lid), global net flux ~ 0.
TEST(SIMPLENonOrthogonalTest, NonOrthogonalMassConservation) {
  const Index n = 10;
  const Mesh mesh = cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.45 / static_cast<Real>(n));
  const SIMPLEResult result = solveDistortedCavity(n, 0.45, 2, GradientScheme::LeastSquares);
  ASSERT_EQ(result.status, SIMPLEStatus::Converged);
  const auto continuity = cfd::physics::evaluateContinuity(mesh, result.massFlux);
  EXPECT_LE(continuity.maxCellImbalance, 1e-6);
  EXPECT_LE(std::abs(continuity.globalNetFlux), 1e-12);
  Real maxWallFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(result.massFlux[faceId], 0.0, 1e-12) << patch.name();
      maxWallFlux = std::max(maxWallFlux, std::abs(result.massFlux[faceId]));
    }
  }
  std::printf(
      "\nMass conservation, distorted cavity 10x10 0.45h N=2 LS: max cell imbalance %.3g,"
      " global net flux %.3g, max |wall face flux| %.3g\n",
      continuity.maxCellImbalance, continuity.globalNetFlux, maxWallFlux);
}

TEST(SIMPLENonOrthogonalTest, NonOrthogonalDeterministic) {
  const SIMPLEResult a = solveDistortedCavity(8, 0.45, 3, GradientScheme::GreenGauss);
  const SIMPLEResult b = solveDistortedCavity(8, 0.45, 3, GradientScheme::GreenGauss);
  ASSERT_EQ(a.status, SIMPLEStatus::Converged);
  expectBitIdentical(a, b);
  EXPECT_EQ(a.pressureCorrectionPasses, b.pressureCorrectionPasses);
}

// Exactly the intended number of passes run: N (or 1 for N = 0) momentum-
// predictor solves and pressure-correction solves per outer iteration,
// pinned on a fixed iteration budget.
TEST(SIMPLENonOrthogonalTest, NonOrthogonalCorrectionPassCount) {
  const Mesh mesh = cfd::test::createDistortedQuad2D(6, 6, 1.0, 1.0, 0.3 / 6.0);
  for (const Index corrections : {0u, 1u, 2u, 4u}) {
    SIMPLESettings settings = cavitySettings();
    settings.maxIterations = 7;
    settings.nonOrthogonalCorrections = corrections;
    settings.gradientScheme = GradientScheme::LeastSquares;
    const SIMPLEResult result = solveCavity(mesh, settings);
    ASSERT_EQ(result.iterations, 7u) << "N=" << corrections;
    const Index expected = 7u * std::max<Index>(1, corrections);
    EXPECT_EQ(result.pressureCorrectionPasses, expected) << "N=" << corrections;
    EXPECT_EQ(result.momentumPredictorPasses, expected) << "N=" << corrections;
  }
}

// Accuracy, end to end: plane Couette flow u = (y, 0), p = const is an
// exact steady Navier-Stokes solution; imposed through exact per-face
// MovingWall values on distorted meshes. The corrected solve (N >= 1) must
// be materially more accurate than the uncorrected one, and converge
// faster under refinement. Measured values printed as evidence. The linear
// solves use the existing Jacobi preconditioner option: the
// unpreconditioned BiCGSTAB pressure solve has a PRE-EXISTING breakdown on
// some runs of this case, including on the untouched Cartesian path
// (results/p12-num-003/summary.md).
TEST(SIMPLENonOrthogonalTest, CorrectedSolveIsCloserToExactCouetteFlowOnDistortedMesh) {
  const auto couette = [](const Vector2& p) { return Vector2{p.y, 0.0}; };
  const FluidProperties fluid(1.0, 1.0);
  Real errorUncorrected[2] = {0.0, 0.0};
  Real errorCorrected[2] = {0.0, 0.0};
  const Index grids[2] = {8, 16};
  for (int g = 0; g < 2; ++g) {
    const Index n = grids[g];
    const Mesh mesh = cfd::test::perFaceBoundaryMesh(
        cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.25 / static_cast<Real>(n)));
    BoundaryConditionSet velocityBoundaries;
    for (const auto& patch : mesh.boundaryPatches()) {
      velocityBoundaries.set(mesh, patch.name(),
                             std::make_unique<cfd::boundary::MovingWall>(
                                 couette(mesh.face(patch.faceIds().front()).centroid())));
    }
    const auto pressureBoundaries = zeroGradientPressure(mesh);
    for (const Index corrections : {0u, 1u}) {
      SIMPLESettings settings = cavitySettings();
      settings.maxIterations = 5000;
      settings.velocityTolerance = 1e-7;
      settings.pressureTolerance = 1e-7;
      settings.continuityTolerance = 1e-7;
      settings.momentumSolver.maxIterations = 500;
      settings.momentumSolver.absoluteTolerance = 1e-10;
      settings.momentumSolver.relativeTolerance = 1e-8;
      settings.pressureSolver.maxIterations = 2000;
      settings.pressureSolver.absoluteTolerance = 1e-10;
      settings.pressureSolver.relativeTolerance = 1e-8;
      // Jacobi: the existing linear-solver option that avoids the
      // pre-existing unpreconditioned-BiCGSTAB breakdown on this case.
      settings.pressureSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
      settings.momentumSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
      settings.gradientScheme = GradientScheme::LeastSquares;
      settings.nonOrthogonalCorrections = corrections;
      const SIMPLE simple(settings, 0);
      const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                               VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                                               ScalarField(mesh.numberOfCells(), 0.0));
      ASSERT_EQ(result.status, SIMPLEStatus::Converged) << "n=" << n << " N=" << corrections;
      Real sumSquares = 0.0;
      Real volume = 0.0;
      for (const auto& cell : mesh.cells()) {
        const Real e = cfd::magnitude(result.velocity[cell.id()] - couette(cell.centroid()));
        sumSquares += e * e * cell.volume();
        volume += cell.volume();
      }
      (corrections == 0 ? errorUncorrected : errorCorrected)[g] = std::sqrt(sumSquares / volume);
    }
  }
  std::printf(
      "\nCouette u=(y,0), distorted 0.25h, SIMPLE L2 velocity error: uncorrected %.4g -> %.4g"
      " (order %.3f), corrected %.4g -> %.4g (order %.3f)\n",
      errorUncorrected[0], errorUncorrected[1],
      std::log2(errorUncorrected[0] / errorUncorrected[1]), errorCorrected[0], errorCorrected[1],
      std::log2(errorCorrected[0] / errorCorrected[1]));
  EXPECT_LT(errorCorrected[1], 0.6 * errorUncorrected[1]);
  EXPECT_GT(std::log2(errorCorrected[0] / errorCorrected[1]),
            std::log2(errorUncorrected[0] / errorUncorrected[1]) + 0.5);
}

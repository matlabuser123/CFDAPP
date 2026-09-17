// P12-NUM-004: SIMPLE's robustness features on genuine small solver cases
// (no fabricated residuals):
//   - default settings = the pre-P12-NUM-004 solver, bit for bit;
//   - normalized-residual convergence, quantitatively;
//   - Stagnated on a genuine round-off plateau, not MaxIterations;
//   - Diverging on a genuine finite runaway (aggressive fixed relaxation);
//   - adaptive relaxation recovering that runaway case to convergence;
//   - the P12-COMP-002 BiCGSTAB pressure breakdown recovered by the
//     linear-solver fallback, the outer solve continuing;
//   - determinism of statuses, fallback choices and relaxation histories.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

#include "cfd/algebra/LinearSolverFallback.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::solver::ConvergenceCriterion;

namespace {

struct Case {
  Mesh mesh;
  FluidProperties fluid;
  BoundaryConditionSet velocity;
  BoundaryConditionSet pressure;
};

// Lid-driven cavity, rho = 1, lid u = 1, all-zero-gradient pressure.
Case cavity(Index n, Real mu) {
  Case c{MeshGeometry::createCartesian2D(n, n, 1.0, 1.0), FluidProperties(1.0, mu), {}, {}};
  c.velocity.set(c.mesh, "left", std::make_unique<cfd::boundary::Wall>());
  c.velocity.set(c.mesh, "right", std::make_unique<cfd::boundary::Wall>());
  c.velocity.set(c.mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  c.velocity.set(c.mesh, "top", std::make_unique<cfd::boundary::MovingWall>(Vector2{1.0, 0.0}));
  for (const auto& patch : c.mesh.boundaryPatches()) {
    c.pressure.set(c.mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return c;
}

// The P12-COMP-002 channel geometry and physics (cases/compressible_channel_coupled,
// incompressible part): 48x8 on 1.0 x 0.05, inlet 20 m/s, fixed-pressure
// outlet -- whose symmetric pressure-correction matrix makes unpreconditioned
// BiCGSTAB break down (results/p12-comp-002/summary.md).
Case comp002Channel() {
  Case c{MeshGeometry::createCartesian2D(48, 8, 1.0, 0.05),
         FluidProperties(1.176624, 0.58831),
         {},
         {}};
  c.velocity.set(c.mesh, "left", std::make_unique<cfd::boundary::Inlet>(Vector2{20.0, 0.0}));
  c.velocity.set(c.mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  c.velocity.set(c.mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  c.velocity.set(c.mesh, "top", std::make_unique<cfd::boundary::Wall>());
  c.pressure.set(c.mesh, "left", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  c.pressure.set(c.mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
  c.pressure.set(c.mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  c.pressure.set(c.mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  return c;
}

// cases/compressible_channel_coupled/solver.json, with the pressure solver
// set back to the BiCGSTAB of the original P12-COMP-002 failure.
SIMPLESettings comp002Settings(Index maxIterations) {
  SIMPLESettings s;
  s.maxIterations = maxIterations;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5;
  s.pressureTolerance = 5e-4;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.momentumSolver.maxIterations = 500;
  s.pressureSolver.type = cfd::algebra::LinearSolverType::BiCGSTAB;
  s.pressureSolver.absoluteTolerance = 1e-8;
  s.pressureSolver.relativeTolerance = 1e-6;
  s.pressureSolver.maxIterations = 2000;
  return s;
}

SIMPLESettings cavitySettings(Real alphaU, Real alphaP, Index maxIterations, Real tolerance) {
  SIMPLESettings s;
  s.maxIterations = maxIterations;
  s.velocityRelaxation = alphaU;
  s.pressureRelaxation = alphaP;
  s.velocityTolerance = tolerance;
  s.pressureTolerance = tolerance;
  s.continuityTolerance = tolerance;
  return s;
}

SIMPLEResult solve(const Case& c, const SIMPLESettings& settings) {
  const SIMPLE simple(settings, 0);
  return simple.solve(c.mesh, c.fluid, c.velocity, c.pressure,
                      VectorField(c.mesh.numberOfCells(), Vector2{0.0, 0.0}),
                      ScalarField(c.mesh.numberOfCells(), 0.0));
}

void expectBitIdentical(const SIMPLEResult& a, const SIMPLEResult& b) {
  ASSERT_EQ(a.status, b.status);
  ASSERT_EQ(a.iterations, b.iterations);
  for (Index k = 0; k < a.uResidualHistory.size(); ++k) {
    EXPECT_EQ(a.uResidualHistory[k], b.uResidualHistory[k]);
    EXPECT_EQ(a.vResidualHistory[k], b.vResidualHistory[k]);
    EXPECT_EQ(a.pressureResidualHistory[k], b.pressureResidualHistory[k]);
    EXPECT_EQ(a.continuityHistory[k], b.continuityHistory[k]);
  }
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y);
    EXPECT_EQ(a.pressure[i], b.pressure[i]);
  }
  for (Index f = 0; f < a.massFlux.size(); ++f) EXPECT_EQ(a.massFlux[f], b.massFlux[f]);
}

bool allFinite(const SIMPLEResult& r) {
  for (Index i = 0; i < r.velocity.size(); ++i) {
    if (!std::isfinite(r.velocity[i].x) || !std::isfinite(r.velocity[i].y) ||
        !std::isfinite(r.pressure[i])) {
      return false;
    }
  }
  return true;
}

void printSummary(const char* label, const SIMPLEResult& r) {
  const auto& d = r.robustness;
  std::printf(
      "\n%s: status %d, %llu iterations, final u %.3g v %.3g p %.3g continuity %.3g, alpha "
      "%.4g/%.4g"
      " (min %.4g), increases %llu decreases %llu, fallbacks %llu (recovered %llu)%s%s",
      label, static_cast<int>(r.status), static_cast<unsigned long long>(r.iterations),
      r.finalUResidual, r.finalVResidual, r.finalPressureResidual, r.finalContinuityResidual,
      d.velocityRelaxationHistory.empty() ? 0.0 : d.velocityRelaxationHistory.back(),
      d.pressureRelaxationHistory.empty() ? 0.0 : d.pressureRelaxationHistory.back(),
      d.velocityRelaxationHistory.empty() ? 0.0
                                          : *std::min_element(d.velocityRelaxationHistory.begin(),
                                                              d.velocityRelaxationHistory.end()),
      static_cast<unsigned long long>(d.relaxationIncreases),
      static_cast<unsigned long long>(d.relaxationDecreases),
      static_cast<unsigned long long>(d.linearSolverFallbacks),
      static_cast<unsigned long long>(d.linearSolverFallbackRecoveries),
      d.statusDetail.empty() ? "" : "\n  detail: ", d.statusDetail.c_str());
}

}  // namespace

// Default robustness settings (and any detector enabled on a healthy case
// where it does not fire, and the fallback on a case that never needs it)
// give the pre-P12-NUM-004 solver bit for bit; the always-reported
// normalized histories are finite, one entry per iteration.
TEST(SIMPLERobustnessTest, DefaultRobustnessPreservesBaseline) {
  const Case c = cavity(4, 0.01);
  const SIMPLESettings base = cavitySettings(0.7, 0.3, 500, 1e-6);
  const SIMPLEResult reference = solve(c, base);
  ASSERT_EQ(reference.status, SIMPLEStatus::Converged);

  SIMPLESettings explicitDefault = base;
  explicitDefault.robustness = cfd::solver::SolverRobustnessSettings{};
  expectBitIdentical(reference, solve(c, explicitDefault));

  SIMPLESettings detectorsAndFallback = base;
  detectorsAndFallback.robustness.stagnation.enabled = true;
  detectorsAndFallback.robustness.divergence.enabled = true;
  detectorsAndFallback.robustness.linearSolverFallback.enabled = true;
  detectorsAndFallback.robustness.linearSolverFallback.maxAttempts = 3;
  const SIMPLEResult guarded = solve(c, detectorsAndFallback);
  expectBitIdentical(reference, guarded);
  EXPECT_EQ(guarded.robustness.linearSolverFallbacks, 0u);
  EXPECT_TRUE(guarded.robustness.statusDetail.empty());

  const auto& d = reference.robustness;
  EXPECT_EQ(d.convergenceCriterion, ConvergenceCriterion::Absolute);
  ASSERT_EQ(d.uNormalizedHistory.size(), reference.iterations);
  ASSERT_EQ(d.velocityRelaxationHistory.size(), reference.iterations);
  for (Index k = 0; k < reference.iterations; ++k) {
    EXPECT_TRUE(std::isfinite(d.uNormalizedHistory[k]));
    EXPECT_TRUE(std::isfinite(d.vNormalizedHistory[k]));
    EXPECT_TRUE(std::isfinite(d.pressureNormalizedHistory[k]));
    EXPECT_TRUE(std::isfinite(d.continuityNormalizedHistory[k]));
    EXPECT_EQ(d.velocityRelaxationHistory[k], 0.7);
    EXPECT_EQ(d.pressureRelaxationHistory[k], 0.3);
  }
  // v starts at exactly 0 (cavity at rest): the zero-baseline policy.
  EXPECT_EQ(reference.vResidualHistory[0], 0.0);
  EXPECT_EQ(d.vNormalizedHistory[0], 0.0);
}

// Normalized convergence decision, quantitatively: at the reported
// iteration every gate holds (u, v <= max(1e-4 * ref, tol), p likewise,
// continuity and global imbalance <= their absolute tolerance), and at the
// previous iteration at least one did not; the absolute run on the same
// case needs more iterations.
TEST(SIMPLERobustnessTest, NormalizedResidualConvergence) {
  const Case c = cavity(8, 0.01);
  SIMPLESettings settings = cavitySettings(0.7, 0.3, 3000, 1e-8);
  const SIMPLEResult absolute = solve(c, settings);
  settings.robustness.convergenceCriterion = ConvergenceCriterion::Normalized;
  settings.robustness.normalization.velocityTolerance = 1e-4;
  settings.robustness.normalization.pressureTolerance = 1e-4;
  const SIMPLEResult normalized = solve(c, settings);
  ASSERT_EQ(normalized.status, SIMPLEStatus::Converged);
  ASSERT_EQ(absolute.status, SIMPLEStatus::Converged);
  EXPECT_LT(normalized.iterations, absolute.iterations);

  const auto& d = normalized.robustness;
  const Index last = normalized.iterations - 1;
  const auto gatesHold = [&](Index k) {
    return normalized.uResidualHistory[k] <= std::max(1e-4 * d.uReference, 1e-8) &&
           normalized.vResidualHistory[k] <= std::max(1e-4 * d.vReference, 1e-8) &&
           normalized.pressureResidualHistory[k] <= std::max(1e-4 * d.pressureReference, 1e-8) &&
           normalized.continuityHistory[k] <= 1e-8;
  };
  EXPECT_TRUE(gatesHold(last));
  EXPECT_FALSE(gatesHold(last - 1));
  EXPECT_LE(d.uNormalizedHistory[last], 1e-4);
  EXPECT_LE(d.pressureNormalizedHistory[last], 1e-4);
  EXPECT_LE(normalized.globalMassImbalance, 1e-8);
  std::printf(
      "\nCavity 8x8 Re=100: absolute (1e-8) %llu iterations; normalized (1e-4, references u %.4g"
      " v %.4g p %.4g) %llu iterations, final normalized u %.3g v %.3g p %.3g\n",
      static_cast<unsigned long long>(absolute.iterations), d.uReference, d.vReference,
      d.pressureReference, static_cast<unsigned long long>(normalized.iterations),
      d.uNormalizedHistory[last], d.vNormalizedHistory[last], d.pressureNormalizedHistory[last]);
}

// A genuine plateau: tolerances below the attainable round-off level
// (1e-16). Without detection the solve burns its whole budget and reports
// MaxIterations; with detection it stops as Stagnated once the residuals
// have frozen at their round-off fixed point.
TEST(SIMPLERobustnessTest, StagnationStatus) {
  const Case c = cavity(8, 0.01);
  SIMPLESettings settings = cavitySettings(0.7, 0.3, 6000, 1e-16);
  const SIMPLEResult plain = solve(c, settings);
  settings.robustness.stagnation.enabled = true;  // defaults: window 50, 1%, start 100
  const SIMPLEResult detected = solve(c, settings);
  printSummary("Round-off plateau, detection off", plain);
  printSummary("Round-off plateau, detection on", detected);
  std::printf("\n");
  EXPECT_EQ(plain.status, SIMPLEStatus::MaxIterations);
  EXPECT_EQ(plain.iterations, 6000u);
  ASSERT_EQ(detected.status, SIMPLEStatus::Stagnated);
  EXPECT_LT(detected.iterations, 4000u);
  EXPECT_TRUE(allFinite(detected));
  EXPECT_NE(detected.robustness.statusDetail.find("stagnation"), std::string::npos);
  // The plateau is genuine: the last window of the detected run equals the
  // final residuals of the 6000-iteration run.
  EXPECT_EQ(detected.finalUResidual, plain.finalUResidual);
  EXPECT_EQ(detected.finalPressureResidual, plain.finalPressureResidual);
}

// Normal (not slow-to-the-point-of-useless) convergence is never flagged:
// the default-relaxation cavity converges with every detector enabled at
// default settings, in exactly the iterations of the plain run.
TEST(SIMPLERobustnessTest, NormalConvergenceIsNotStagnationOrDivergence) {
  const Case c = cavity(8, 0.01);
  SIMPLESettings settings = cavitySettings(0.7, 0.3, 3000, 1e-6);
  const SIMPLEResult plain = solve(c, settings);
  settings.robustness.stagnation.enabled = true;
  settings.robustness.divergence.enabled = true;
  const SIMPLEResult guarded = solve(c, settings);
  ASSERT_EQ(plain.status, SIMPLEStatus::Converged);
  expectBitIdentical(plain, guarded);
}

// Aggressive fixed relaxation (alpha_u = alpha_p = 0.9) on the 8x8 Re=100
// cavity: a genuine finite runaway (residuals grow ~10x per 5 iterations).
// Divergence detection stops it at the earliest possible iteration
// (startIteration 10 + window 10 = 20) with finite fields.
TEST(SIMPLERobustnessTest, DivergenceStatus) {
  const Case c = cavity(8, 0.01);
  SIMPLESettings settings = cavitySettings(0.9, 0.9, 400, 1e-6);
  settings.robustness.divergence.enabled = true;  // window 10, factor 10, start 10
  const SIMPLEResult result = solve(c, settings);
  printSummary("Aggressive fixed relaxation 0.9/0.9, divergence detection on", result);
  std::printf("\n");
  ASSERT_EQ(result.status, SIMPLEStatus::Diverging);
  // P12-DIFF-002 A6-1 (validation-migration/acceptance_gate_A6.md §1). This used to assert
  // `iterations == 20`. Twenty is the EARLIEST iteration at which the windowed rules may fire at
  // all -- `record()` does not evaluate them before
  //     max(divergence.startIteration, 1) + divergence.window
  // (SolverRobustness.cpp:481-482) -- so the historical value was a lower bound asserted as if it
  // were a prediction. Which iteration the detector actually reaches depends on the trajectory and
  // on which of the two growth rules trips first: measured 20/20/21/20/20 across 4x4...16x16 and
  // 11-41 across a sweep of window/startIteration/growthFactor, with rule 1 and rule 2 alternating
  // (results/p12-diff-002/validation-migration/a6/resumed/logs/02).
  //
  // Both bounds below are derived from the settings, not from an observation:
  //   lower  max(start, 1) + window       -- the rules cannot be evaluated any earlier
  //   upper  max(start, 1) + 2 * window   -- with b the minimum over [start, start + window), once
  //          every later value exceeds growth * b the whole window is above the threshold and the
  //          persistent-excursion rule fires; this run's growth (>= 1e3 overall, asserted below)
  //          guarantees that.
  // The one path that can stop earlier -- a residual norm overflowing to non-finite -- cannot apply
  // here, because allFinite(result) is asserted.
  const auto& divergence = settings.robustness.divergence;
  const Index earliestPossible = std::max<Index>(divergence.startIteration, 1) + divergence.window;
  const Index guaranteedBy = earliestPossible + divergence.window;
  EXPECT_EQ(earliestPossible, 20u) << "the historical value, now asserted as the bound it is";
  EXPECT_GE(result.iterations, earliestPossible);
  EXPECT_LE(result.iterations, guaranteedBy);
  EXPECT_TRUE(allFinite(result));
  EXPECT_GT(result.finalUResidual, 1e3 * result.uResidualHistory.front());
  EXPECT_NE(result.robustness.statusDetail.find("divergence"), std::string::npos);
}

// Adaptive relaxation recovery: the same aggressive start that diverges
// with fixed relaxation (previous test) converges when the controller may
// reduce alpha (bounds [0.2, 0.95] x [0.05, 0.95]).
TEST(SIMPLERobustnessTest, AdaptiveRelaxationRecovery) {
  const Case c = cavity(8, 0.01);
  SIMPLESettings fixed = cavitySettings(0.9, 0.9, 3000, 1e-6);
  fixed.robustness.divergence.enabled = true;
  SIMPLESettings adaptive = cavitySettings(0.9, 0.9, 3000, 1e-6);
  adaptive.robustness.adaptiveRelaxation.enabled = true;
  adaptive.robustness.adaptiveRelaxation.minVelocity = 0.2;
  adaptive.robustness.adaptiveRelaxation.maxVelocity = 0.95;
  adaptive.robustness.adaptiveRelaxation.minPressure = 0.05;
  adaptive.robustness.adaptiveRelaxation.maxPressure = 0.95;
  const SIMPLEResult fixedResult = solve(c, fixed);
  const SIMPLEResult adaptiveResult = solve(c, adaptive);
  // The conservative fixed relaxation, for scale.
  const SIMPLEResult conservative = solve(c, cavitySettings(0.7, 0.3, 3000, 1e-6));
  printSummary("Fixed 0.9/0.9", fixedResult);
  printSummary("Adaptive from 0.9/0.9", adaptiveResult);
  printSummary("Fixed 0.7/0.3 (conservative reference)", conservative);
  std::printf("\n");
  EXPECT_EQ(fixedResult.status, SIMPLEStatus::Diverging);
  ASSERT_EQ(adaptiveResult.status, SIMPLEStatus::Converged);
  const auto& d = adaptiveResult.robustness;
  EXPECT_GE(d.relaxationDecreases, 1u);
  const Real minAlpha =
      *std::min_element(d.velocityRelaxationHistory.begin(), d.velocityRelaxationHistory.end());
  EXPECT_LT(minAlpha, 0.9);
  for (Index k = 0; k < adaptiveResult.iterations; ++k) {
    EXPECT_GE(d.velocityRelaxationHistory[k], 0.2);
    EXPECT_LE(d.velocityRelaxationHistory[k], 0.95);
    EXPECT_GE(d.pressureRelaxationHistory[k], 0.05);
    EXPECT_LE(d.pressureRelaxationHistory[k], 0.95);
  }
  // Relaxation changes only between iterations: one value per iteration.
  EXPECT_EQ(d.velocityRelaxationHistory.size(), adaptiveResult.iterations);
  EXPECT_TRUE(allFinite(adaptiveResult));
  EXPECT_LE(adaptiveResult.finalContinuityResidual, 1e-6);
}

// The P12-COMP-002 channel's BiCGSTAB pressure solve. Before P12-MESH-004
// the unpreconditioned BiCGSTAB pressure solve reported Breakdown in outer
// iteration 33 (PressureCorrectionFailure after 32 iterations), and the
// fallback (CG, SPD proven) recovered it. That breakdown was the
// scale-dependent false breakdown P12-MESH-004 fixed: t . t = 5.6e-31 fell
// below the absolute 1e-30 only because |t| / |s| = 3.9e-8 (the pressure-
// correction matrix's own scale) times |s| = 1.9e-8 (the residual's) --
// results/p12-mesh-004/solver-robustness/05. With the scale-invariant test
// the solve never breaks down: the outer solve runs to its budget with no
// linear failure, and enabling the fallback changes nothing (it is never
// needed). The fallback policy on a GENUINE breakdown is covered by
// LinearFallbackTest (tests/unit/algebra/test_linear_solver_fallback.cpp).
TEST(SIMPLERobustnessTest, LinearSolverFallbackRecovery) {
  const Case c = comp002Channel();
  SIMPLESettings settings = comp002Settings(60);
  const SIMPLEResult without = solve(c, settings);
  settings.robustness.linearSolverFallback.enabled = true;
  const SIMPLEResult with = solve(c, settings);
  printSummary("P12-COMP-002 channel, BiCGSTAB pressure, fallback off", without);
  printSummary("P12-COMP-002 channel, BiCGSTAB pressure, fallback on", with);
  std::printf("\n");

  EXPECT_EQ(without.status, SIMPLEStatus::MaxIterations);  // ran to the budget
  EXPECT_EQ(without.iterations, 60u);
  EXPECT_TRUE(without.robustness.statusDetail.empty());  // no linear-solver failure
  EXPECT_TRUE(allFinite(without));
  EXPECT_EQ(with.robustness.linearSolverFallbacks, 0u);
  EXPECT_TRUE(with.robustness.fallbackEvents.empty());
  expectBitIdentical(without, with);
}

// Repeated identical runs with every feature enabled give identical
// statuses, residual histories, relaxation histories and fallback choices.
TEST(SIMPLERobustnessTest, Deterministic) {
  {
    const Case c = cavity(8, 0.01);
    SIMPLESettings settings = cavitySettings(0.9, 0.9, 3000, 1e-6);
    settings.robustness.adaptiveRelaxation.enabled = true;
    settings.robustness.adaptiveRelaxation.minVelocity = 0.2;
    settings.robustness.adaptiveRelaxation.maxVelocity = 0.95;
    settings.robustness.adaptiveRelaxation.maxPressure = 0.95;
    settings.robustness.stagnation.enabled = true;
    settings.robustness.linearSolverFallback.enabled = true;
    const SIMPLEResult a = solve(c, settings);
    const SIMPLEResult b = solve(c, settings);
    expectBitIdentical(a, b);
    EXPECT_EQ(a.robustness.velocityRelaxationHistory, b.robustness.velocityRelaxationHistory);
    EXPECT_EQ(a.robustness.pressureRelaxationHistory, b.robustness.pressureRelaxationHistory);
    EXPECT_EQ(a.robustness.uNormalizedHistory, b.robustness.uNormalizedHistory);
  }
  {
    const Case c = comp002Channel();
    SIMPLESettings settings = comp002Settings(45);
    settings.robustness.linearSolverFallback.enabled = true;
    const SIMPLEResult a = solve(c, settings);
    const SIMPLEResult b = solve(c, settings);
    expectBitIdentical(a, b);
    ASSERT_EQ(a.robustness.fallbackEvents.size(), b.robustness.fallbackEvents.size());
    for (Index k = 0; k < a.robustness.fallbackEvents.size(); ++k) {
      const auto& x = a.robustness.fallbackEvents[k];
      const auto& y = b.robustness.fallbackEvents[k];
      EXPECT_EQ(x.iteration, y.iteration);
      EXPECT_EQ(x.equation, y.equation);
      EXPECT_EQ(x.report.attempts.size(), y.report.attempts.size());
      EXPECT_EQ(x.report.attempts.back().type, y.report.attempts.back().type);
      EXPECT_EQ(x.report.attempts.back().iterations, y.report.attempts.back().iterations);
    }
  }
}

// Invalid robustness settings are a solve()-time InvalidConfiguration, like
// every other invalid setting.
TEST(SIMPLERobustnessTest, InvalidRobustnessSettingsAreInvalidConfiguration) {
  const Case c = cavity(4, 0.01);
  SIMPLESettings settings = cavitySettings(0.7, 0.3, 10, 1e-6);
  settings.robustness.divergence.growthFactor = 1.0;
  EXPECT_EQ(solve(c, settings).status, SIMPLEStatus::InvalidConfiguration);
  settings = cavitySettings(0.95, 0.3, 10, 1e-6);  // 0.95 > default max 0.9
  settings.robustness.adaptiveRelaxation.enabled = true;
  EXPECT_EQ(solve(c, settings).status, SIMPLEStatus::InvalidConfiguration);
}

// Performance: the same healthy solve with every bookkeeping feature on
// (detectors evaluated every iteration, adaptive controller, fallback
// wrapper) vs. off -- identical iterations; wall times printed (debug
// build, not asserted: machine-dependent).
TEST(SIMPLERobustnessTest, BookkeepingOverheadIsSmall) {
  // 8x8, 1e-6: a run with no linear-solver breakdown at all (the 16x16
  // cavity at tight tolerances hits the pre-existing BiCGSTAB pressure
  // breakdown, which the fallback would change -- not a like-for-like
  // timing).
  const Case c = cavity(8, 0.01);
  SIMPLESettings off = cavitySettings(0.7, 0.3, 3000, 1e-6);
  SIMPLESettings on = off;
  on.robustness.stagnation.enabled = true;  // evaluated from iteration 100, never fires here
  on.robustness.divergence.enabled = true;  // evaluated from iteration 20, never fires here
  on.robustness.linearSolverFallback.enabled = true;
  const auto time = [&](const SIMPLESettings& s, SIMPLEResult& out) {
    const auto start = std::chrono::steady_clock::now();
    out = solve(c, s);
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  };
  SIMPLEResult a;
  SIMPLEResult b;
  const double tOff = time(off, a);
  const double tOn = time(on, b);
  expectBitIdentical(a, b);
  std::printf(
      "\nCavity 8x8, %llu iterations: robustness off %.3f s, detectors+fallback on %.3f s"
      " (ratio %.3f)\n",
      static_cast<unsigned long long>(a.iterations), tOff, tOn, tOn / tOff);
}

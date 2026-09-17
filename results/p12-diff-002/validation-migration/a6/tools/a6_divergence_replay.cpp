// P12-DIFF-002 A6 Step 1: independent replay of BOTH divergence rules on the continuity-residual
// history of the converging cavity(4, 0.01) run, to establish exactly which rule aborts it and
// whether an absolute floor would prevent that.
//
// Investigation only. Nothing is modified; no value is adopted as an expectation.
//
// The two rules, transcribed from src/solver/SolverRobustness.cpp::OuterIterationMonitor::diverging
// (read-only), with window w = divergence.window and g = divergence.growthFactor:
//
//   RULE 1 (lines 389-400), FLOORED:
//       base = max(tracker.floor(), bestBeforeWindow)        <- floor() IS the convergence tolerance
//       fires when  windowMin >= g * base
//
//   RULE 2 (lines 401-415), NOT FLOORED:
//       fires when  the last w values increase strictly at every step
//                   AND  newest >= g * oldest                <- `oldest` used raw
//
// Rule 2 therefore compares two residuals that rule 1 would both regard as already converged.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using namespace cfd;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

constexpr Real kTolerance = 1e-6;  // the case's velocity/pressure/continuity tolerance
constexpr Index kWindow = 10;      // divergence.window
constexpr Index kStart = 10;       // divergence.startIteration
constexpr Real kGrowth = 10.0;     // divergence.growthFactor

struct Fire {
  Index at{0};
  Real a{0.0};
  Real b{0.0};
};

// RULE 2, transcribed: strict increase across the window, newest >= g * oldest, no floor.
Fire rule2(const std::vector<Real>& h, Real floor) {
  for (Index k = kWindow; k <= h.size(); ++k) {
    bool increasing = true;
    for (Index i = k - kWindow + 1; i < k; ++i) {
      if (!(h[i] > h[i - 1])) {
        increasing = false;
        break;
      }
    }
    const Real oldest = std::max(h[k - kWindow], floor);
    const Real newest = h[k - 1];
    if (increasing && newest >= kGrowth * oldest) return Fire{k, h[k - kWindow], newest};
  }
  return Fire{};
}

// RULE 1, transcribed: floored base, window minimum must exceed g * base.
Fire rule1(const std::vector<Real>& h, Real floor) {
  for (Index k = kStart + kWindow; k <= h.size(); ++k) {
    Real bestBeforeWindow = std::numeric_limits<Real>::infinity();
    for (Index i = kStart; i + kWindow < k + 1 && i < k - kWindow; ++i) {
      bestBeforeWindow = std::min(bestBeforeWindow, h[i]);
    }
    const Real base = std::max(floor, bestBeforeWindow);
    Real windowMin = std::numeric_limits<Real>::infinity();
    for (Index i = k - kWindow; i < k; ++i) windowMin = std::min(windowMin, h[i]);
    if (std::isfinite(base) && windowMin >= kGrowth * base) return Fire{k, base, windowMin};
  }
  return Fire{};
}

void report(const char* label, const Fire& f) {
  if (f.at == 0) {
    std::printf("    %-46s does NOT fire\n", label);
  } else {
    std::printf("    %-46s fires at iteration %zu   (%.4e -> %.4e)\n", label,
                static_cast<std::size_t>(f.at), f.a, f.b);
  }
}

}  // namespace

int main() {
  auto mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  boundary::BoundaryConditionSet velocity;
  boundary::BoundaryConditionSet pressure;
  velocity.set(mesh, "left", std::make_unique<boundary::Wall>());
  velocity.set(mesh, "right", std::make_unique<boundary::Wall>());
  velocity.set(mesh, "bottom", std::make_unique<boundary::Wall>());
  velocity.set(mesh, "top", std::make_unique<boundary::MovingWall>(Vector2{1.0, 0.0}));
  for (const auto& patch : mesh.boundaryPatches()) {
    pressure.set(mesh, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
  }

  SIMPLESettings s;
  s.maxIterations = 500;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = kTolerance;
  s.pressureTolerance = kTolerance;
  s.continuityTolerance = kTolerance;

  const SIMPLE simple(s, 0);
  const auto r = simple.solve(mesh, fluid, velocity, pressure,
                              fields::VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                              fields::ScalarField(mesh.numberOfCells(), 0.0));

  std::printf("# Reference run (NO detectors): status %d, %zu iterations, final continuity %.4e\n",
              static_cast<int>(r.status), static_cast<std::size_t>(r.iterations),
              r.finalContinuityResidual);
  std::printf("# convergence tolerance (= ResidualTracker::floor for the continuity tracker):"
              " %.1e\n\n", kTolerance);

  const std::vector<Real>& c = r.continuityHistory;
  const std::vector<Real>& u = r.uResidualHistory;

  std::printf("## continuity residual around the iteration the guarded run aborted at (112)\n");
  for (Index k = 100; k < std::min<Index>(c.size(), 118); ++k) {
    std::printf("    [%3zu] continuity %.4e   u %.4e%s\n", static_cast<std::size_t>(k), c[k], u[k],
                (c[k] <= kTolerance) ? "   (continuity already CONVERGED)" : "");
  }

  std::printf("\n## replay of the two rules on the CONTINUITY history\n");
  report("RULE 1 (floored, as production applies it)", rule1(c, kTolerance));
  report("RULE 2 as production applies it (NO floor)", rule2(c, 0.0));
  report("RULE 2 with the same floor rule 1 uses", rule2(c, kTolerance));

  std::printf("\n## replay of the two rules on the U-RESIDUAL history (monotonically decreasing)\n");
  report("RULE 1 (floored)", rule1(u, kTolerance));
  report("RULE 2 (no floor)", rule2(u, 0.0));

  std::printf("\n## how far below tolerance the values that trip rule 2 are\n");
  const Fire f = rule2(c, 0.0);
  if (f.at != 0) {
    std::printf("    oldest %.4e  = tolerance / %.3e\n", f.a, kTolerance / f.a);
    std::printf("    newest %.4e  = tolerance / %.3e\n", f.b, kTolerance / f.b);
    std::printf("    both are below the convergence tolerance, so rule 1's floor would suppress\n"
                "    them; rule 2 has no floor and therefore fires on round-off drift.\n");
  }
  return 0;
}

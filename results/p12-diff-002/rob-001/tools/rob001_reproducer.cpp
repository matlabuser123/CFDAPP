// P12-DIFF-002-ROB-001 Step 2: independent reproducer for the divergence-detector false positive.
//
// It reproduces the 4x4 cavity of SIMPLERobustnessTest.DefaultRobustnessPreservesBaseline directly,
// captures the complete residual histories, and replays BOTH divergence rules -- for every tracked
// residual -- transcribed from src/solver/SolverRobustness.cpp (read-only), in three variants:
//
//   RULE 1            base = max(floor, bestBeforeWindow);  fires when windowMin >= growth * base
//   RULE 2 unfloored  oldest = valueAgo(window-1);           fires when strictly increasing across
//                                                            the window AND newest >= growth*oldest
//   RULE 2 floored    the proposed correction: oldest -> max(floor, oldest)
//
// `floor` is the tracker's own floor, which is the convergence tolerance for that residual
// (SolverRobustness.cpp:356 returns tracker.floor() as the absolute convergence threshold).
//
// It also reports the stagnation detector's own quantities, because stagnation is evaluated AFTER
// divergence (SolverRobustness.cpp:489) and must not become the next thing to fire.
//
// Investigation/verification only: no value printed here is adopted as a test expectation.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

constexpr Index kWindow = 10;   // divergence.window default
constexpr Index kStart = 10;    // divergence.startIteration default
constexpr Real kGrowth = 10.0;  // divergence.growthFactor default

struct Case {
  Mesh mesh;
  FluidProperties fluid;
  boundary::BoundaryConditionSet velocity;
  boundary::BoundaryConditionSet pressure;
};

Case cavity(Index n, Real mu) {
  Case c{MeshGeometry::createCartesian2D(n, n, 1.0, 1.0), FluidProperties(1.0, mu), {}, {}};
  c.velocity.set(c.mesh, "left", std::make_unique<boundary::Wall>());
  c.velocity.set(c.mesh, "right", std::make_unique<boundary::Wall>());
  c.velocity.set(c.mesh, "bottom", std::make_unique<boundary::Wall>());
  c.velocity.set(c.mesh, "top", std::make_unique<boundary::MovingWall>(Vector2{1.0, 0.0}));
  for (const auto& patch : c.mesh.boundaryPatches()) {
    c.pressure.set(c.mesh, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
  }
  return c;
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
                      fields::VectorField(c.mesh.numberOfCells(), Vector2{0.0, 0.0}),
                      fields::ScalarField(c.mesh.numberOfCells(), 0.0));
}

const char* statusName(SIMPLEStatus s) {
  switch (s) {
    case SIMPLEStatus::Converged: return "Converged";
    case SIMPLEStatus::MaxIterations: return "MaxIterations";
    case SIMPLEStatus::Diverging: return "Diverging";
    case SIMPLEStatus::Stagnated: return "Stagnated";
    default: return "other";
  }
}

struct Fire {
  Index at{0};
  Real oldest{0.0};
  Real newest{0.0};
  Real base{0.0};
};

Fire rule1(const std::vector<Real>& h, Real floor) {
  for (Index k = kStart + kWindow; k <= h.size(); ++k) {
    Real bestBeforeWindow = std::numeric_limits<Real>::infinity();
    for (Index i = kStart; i < k - kWindow; ++i) {
      bestBeforeWindow = std::min(bestBeforeWindow, h[i]);
    }
    const Real base = std::max(floor, bestBeforeWindow);
    Real windowMin = std::numeric_limits<Real>::infinity();
    for (Index i = k - kWindow; i < k; ++i) windowMin = std::min(windowMin, h[i]);
    if (std::isfinite(base) && windowMin >= kGrowth * base) {
      return Fire{k, base, windowMin, base};
    }
  }
  return Fire{};
}

// `floor` = 0.0 reproduces production's current rule 2; `floor` = tracker floor is the correction.
Fire rule2(const std::vector<Real>& h, Real floor) {
  for (Index k = kStart + kWindow; k <= h.size(); ++k) {
    bool increasing = true;
    for (Index i = k - kWindow + 1; i < k; ++i) {
      if (!(h[i] > h[i - 1])) {
        increasing = false;
        break;
      }
    }
    const Real rawOldest = h[k - kWindow];
    const Real oldest = std::max(rawOldest, floor);
    const Real newest = h[k - 1];
    if (increasing && newest >= kGrowth * oldest) return Fire{k, rawOldest, newest, oldest};
  }
  return Fire{};
}

void replay(const char* name, const std::vector<Real>& h, Real floor) {
  const Fire f1 = rule1(h, floor);
  const Fire f2raw = rule2(h, 0.0);
  const Fire f2floored = rule2(h, floor);
  std::printf("  %-12s floor %.1e | rule1 %-14s | rule2 UNFLOORED %-14s | rule2 FLOORED %s\n", name,
              floor, f1.at ? ("fires@" + std::to_string(f1.at)).c_str() : "does not fire",
              f2raw.at ? ("fires@" + std::to_string(f2raw.at)).c_str() : "does not fire",
              f2floored.at ? ("fires@" + std::to_string(f2floored.at)).c_str() : "does not fire");
  if (f2raw.at != 0) {
    std::printf("        rule2 unfloored detail: oldest %.4e  newest %.4e  growth ratio %.3f\n",
                f2raw.oldest, f2raw.newest, f2raw.newest / f2raw.oldest);
    std::printf("        both values below the floor? oldest %s, newest %s"
                "   (floor/oldest = %.3e, floor/newest = %.3e)\n",
                f2raw.oldest < floor ? "YES" : "no", f2raw.newest < floor ? "YES" : "no",
                floor / f2raw.oldest, floor / f2raw.newest);
    std::printf("        with the floor applied, the comparison becomes newest %.4e >= %.1f * %.4e"
                " = %.4e -> %s\n",
                f2raw.newest, kGrowth, std::max(f2raw.oldest, floor),
                kGrowth * std::max(f2raw.oldest, floor),
                f2raw.newest >= kGrowth * std::max(f2raw.oldest, floor) ? "still fires" : "SUPPRESSED");
  }
}

}  // namespace

int main() {
  std::printf("# ROB-001 Step 2 -- pre-fix reproducer (run against the UNMODIFIED library).\n");
  std::printf("# cavity(4, 0.01), alpha 0.7/0.3, tol 1e-6, maxIterations 500 --"
              " DefaultRobustnessPreservesBaseline's own case.\n\n");

  const Case c = cavity(4, 0.01);
  const SIMPLESettings base = cavitySettings(0.7, 0.3, 500, 1e-6);
  const SIMPLEResult reference = solve(c, base);

  SIMPLESettings guardedSettings = base;
  guardedSettings.robustness.stagnation.enabled = true;
  guardedSettings.robustness.divergence.enabled = true;
  guardedSettings.robustness.linearSolverFallback.enabled = true;
  guardedSettings.robustness.linearSolverFallback.maxAttempts = 3;
  const SIMPLEResult guarded = solve(c, guardedSettings);

  std::printf("## the two runs the test requires to be bit-identical\n");
  std::printf("  detectors OFF: %-13s %4zu iterations  final u %.4e v %.4e p %.4e cont %.4e\n",
              statusName(reference.status), static_cast<std::size_t>(reference.iterations),
              reference.finalUResidual, reference.finalVResidual, reference.finalPressureResidual,
              reference.finalContinuityResidual);
  std::printf("  detectors ON : %-13s %4zu iterations  final u %.4e v %.4e p %.4e cont %.4e\n",
              statusName(guarded.status), static_cast<std::size_t>(guarded.iterations),
              guarded.finalUResidual, guarded.finalVResidual, guarded.finalPressureResidual,
              guarded.finalContinuityResidual);
  if (!guarded.robustness.statusDetail.empty()) {
    std::printf("  detail: %s\n", guarded.robustness.statusDetail.c_str());
  }
  Real worstU = 0.0;
  Real worstV = 0.0;
  Real worstP = 0.0;
  const Index m = std::min<Index>(reference.velocity.size(), guarded.velocity.size());
  for (Index i = 0; i < m; ++i) {
    worstU = std::max(worstU, std::abs(reference.velocity[i].x - guarded.velocity[i].x));
    worstV = std::max(worstV, std::abs(reference.velocity[i].y - guarded.velocity[i].y));
    worstP = std::max(worstP, std::abs(reference.pressure[i] - guarded.pressure[i]));
  }
  std::printf("  field difference OFF vs ON: u %.4e  v %.4e  p %.4e   (the test requires exactly 0)\n",
              worstU, worstV, worstP);
  std::printf("  same status: %s   same iterations: %s\n",
              reference.status == guarded.status ? "yes" : "NO",
              reference.iterations == guarded.iterations ? "yes" : "NO");

  std::printf("\n## independent replay of BOTH rules, every tracked residual, on the REFERENCE run\n");
  std::printf("## (the reference run is the healthy trajectory the detectors must not touch)\n");
  replay("u", reference.uResidualHistory, base.velocityTolerance);
  replay("v", reference.vResidualHistory, base.velocityTolerance);
  replay("pressure", reference.pressureResidualHistory, base.pressureTolerance);
  replay("continuity", reference.continuityHistory, base.continuityTolerance);

  std::printf("\n## the continuity window that trips rule 2, printed in full\n");
  const Fire f = rule2(reference.continuityHistory, 0.0);
  if (f.at != 0) {
    for (Index i = f.at - kWindow; i < f.at; ++i) {
      std::printf("    [%3zu] continuity %.6e%s\n", static_cast<std::size_t>(i),
                  reference.continuityHistory[i],
                  reference.continuityHistory[i] <= base.continuityTolerance ? "   (converged)"
                                                                            : "");
    }
    std::printf("    convergence tolerance / tracker floor : %.1e\n", base.continuityTolerance);
    std::printf("    u residual at that iteration          : %.6e  (NOT converged -> the solve was"
                " still legitimately running)\n",
                reference.uResidualHistory[f.at - 1]);
  }

  std::printf("\n## stagnation quantities (checked AFTER divergence; must not become the next"
              " firing rule)\n");
  const auto& d = reference.robustness;
  std::printf("  convergence-distance history length %zu; stagnation window %zu,"
              " minRelativeImprovement %.3e\n",
              static_cast<std::size_t>(d.convergenceDistanceHistory.size()),
              static_cast<std::size_t>(guardedSettings.robustness.stagnation.window),
              guardedSettings.robustness.stagnation.minRelativeImprovement);
  if (!d.convergenceDistanceHistory.empty()) {
    const auto& dist = d.convergenceDistanceHistory;
    const Index w = guardedSettings.robustness.stagnation.window;
    Real worstImprovement = 1.0;
    Index worstAt = 0;
    for (Index k = w + 1; k <= dist.size(); ++k) {
      Real bestOld = std::numeric_limits<Real>::infinity();
      for (Index i = 0; i < k - w; ++i) bestOld = std::min(bestOld, dist[i]);
      Real bestNew = std::numeric_limits<Real>::infinity();
      for (Index i = k - w; i < k; ++i) bestNew = std::min(bestNew, dist[i]);
      if (std::isfinite(bestOld) && bestOld > 0.0) {
        const Real improvement = (bestOld - bestNew) / bestOld;
        if (improvement < worstImprovement) {
          worstImprovement = improvement;
          worstAt = k;
        }
      }
    }
    std::printf("  worst windowed relative improvement over the whole run: %.6e at iteration %zu\n",
                worstImprovement, static_cast<std::size_t>(worstAt));
    std::printf("  -> stagnation would %s (threshold %.3e)\n",
                worstImprovement < guardedSettings.robustness.stagnation.minRelativeImprovement
                    ? "FIRE"
                    : "not fire",
                guardedSettings.robustness.stagnation.minRelativeImprovement);
  }
  return 0;
}

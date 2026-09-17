// P12-DIFF-002-ROB-001 Step 8 / criterion R7: sensitivity of the corrected detector.
//
// Verification, not tuning: no parameter is chosen to make anything pass. The question is only
// whether the correction is robust across configurations, i.e.
//   (a) no HEALTHY run is aborted by the detectors (detectors ON must equal detectors OFF), and
//   (b) a GENUINE runaway is still detected,
// across mesh sizes, convergence tolerances and divergence windows.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
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

SIMPLESettings settingsFor(Real alphaU, Real alphaP, Index maxIterations, Real tolerance) {
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

std::size_t healthyFailures = 0;
std::size_t runawayMisses = 0;

// (a) A healthy run: enabling every detector must change nothing at all.
void healthy(Index n, Real tol, Index window, Index startIteration) {
  const Case c = cavity(n, 0.01);
  SIMPLESettings base = settingsFor(0.7, 0.3, 1500, tol);
  const SIMPLEResult off = solve(c, base);
  SIMPLESettings on = base;
  on.robustness.stagnation.enabled = true;
  on.robustness.divergence.enabled = true;
  on.robustness.divergence.window = window;
  on.robustness.divergence.startIteration = startIteration;
  on.robustness.linearSolverFallback.enabled = true;
  on.robustness.linearSolverFallback.maxAttempts = 3;
  const SIMPLEResult run = solve(c, on);

  Real worst = 0.0;
  const Index m = std::min<Index>(off.velocity.size(), run.velocity.size());
  for (Index i = 0; i < m; ++i) {
    worst = std::max(worst, std::abs(off.velocity[i].x - run.velocity[i].x));
    worst = std::max(worst, std::abs(off.velocity[i].y - run.velocity[i].y));
    worst = std::max(worst, std::abs(off.pressure[i] - run.pressure[i]));
  }
  const bool same = off.status == run.status && off.iterations == run.iterations && worst == 0.0;
  if (!same) ++healthyFailures;
  std::printf("  %2lldx%-2lld tol %.0e window %2lld start %2lld | OFF %-13s %4zu it | ON %-13s"
              " %4zu it | field diff %.3e | %s\n",
              static_cast<long long>(n), static_cast<long long>(n), tol,
              static_cast<long long>(window), static_cast<long long>(startIteration),
              statusName(off.status), static_cast<std::size_t>(off.iterations),
              statusName(run.status), static_cast<std::size_t>(run.iterations), worst,
              same ? "inert (as required)" : "*** DETECTORS ALTERED A HEALTHY RUN ***");
  if (!run.robustness.statusDetail.empty()) {
    std::printf("        detail: %s\n", run.robustness.statusDetail.c_str());
  }
}

// (b) A genuine runaway: it must still be caught.
void runaway(Index n, Real tol, Index window, Index startIteration) {
  const Case c = cavity(n, 0.01);
  SIMPLESettings s = settingsFor(0.9, 0.9, 400, tol);
  s.robustness.divergence.enabled = true;
  s.robustness.divergence.window = window;
  s.robustness.divergence.startIteration = startIteration;
  const SIMPLEResult r = solve(c, s);
  const bool caught = r.status == SIMPLEStatus::Diverging;
  const bool finite = std::isfinite(r.finalUResidual);
  const Real growth = r.finalUResidual / r.uResidualHistory.front();
  if (!caught) ++runawayMisses;
  std::printf("  %2lldx%-2lld tol %.0e window %2lld start %2lld | %-13s at %3zu it | growth %.3e |"
              " finite %s | %s\n",
              static_cast<long long>(n), static_cast<long long>(n), tol,
              static_cast<long long>(window), static_cast<long long>(startIteration),
              statusName(r.status), static_cast<std::size_t>(r.iterations), growth,
              finite ? "yes" : "NO", caught ? "detected" : "*** RUNAWAY MISSED ***");
}

}  // namespace

int main() {
  std::printf("# ROB-001 R7 -- sensitivity of the corrected detector. Verification, not tuning.\n\n");

  std::printf("## (a) healthy lid-driven cavity: detectors ON must be observationally inert\n");
  for (const Index n : {4, 6, 8}) healthy(n, 1e-6, 10, 10);
  for (const Real tol : {1e-5, 1e-7, 1e-8}) healthy(4, tol, 10, 10);
  for (const Index window : {5, 10, 20}) healthy(4, 1e-6, window, 10);
  healthy(6, 1e-7, 5, 5);
  healthy(8, 1e-5, 20, 20);

  std::printf("\n## (b) genuine runaway (alpha 0.9/0.9): must still be detected\n");
  for (const Index n : {4, 6, 8}) runaway(n, 1e-6, 10, 10);
  for (const Real tol : {1e-5, 1e-7, 1e-8}) runaway(8, tol, 10, 10);
  for (const Index window : {5, 10, 20}) runaway(8, 1e-6, window, 10);
  runaway(6, 1e-7, 5, 5);
  runaway(4, 1e-5, 20, 20);

  std::printf("\n## totals\n");
  std::printf("  healthy runs altered by the detectors : %zu   (R7 requires 0)\n", healthyFailures);
  std::printf("  genuine runaways missed               : %zu   (R7 requires 0)\n", runawayMisses);
  std::printf("\nR7 RESULT: %s\n",
              (healthyFailures == 0 && runawayMisses == 0) ? "PASS" : "FAIL");
  return (healthyFailures == 0 && runawayMisses == 0) ? 0 : 1;
}

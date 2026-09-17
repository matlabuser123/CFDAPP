// P12-DIFF-002 A6 Step 1: investigation of the two M-E iteration/status baseline tests.
// Investigation only -- no test is modified, and no value printed here is adopted as an expectation.
//
// It reproduces the two cases exactly as tests/solver/simple/test_simple_robustness.cpp builds them
// and reports what the recorded baselines actually depend on:
//   * the residual trajectory that the divergence detector reads;
//   * whether the detector's own condition is genuinely satisfied;
//   * whether the converged solution is unaffected;
//   * sensitivity to the solver tolerance and to the mesh.
//
// Built against BOTH the authoritative library and the historical (pre-DIFF-002) one, so every
// number can be attributed.
#include <algorithm>
#include <cmath>
#include <cstdio>
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

// The divergence detector's own documented condition, transcribed here so the decision can be
// judged independently of production: with window w, factor f and start s, it fires at the first
// iteration k >= s + w for which
//     min(u residuals over the last w) >= f * min(u residuals from s .. k)
// Reported as the earliest such k, or 0 if never.
Index detectorWouldFireAt(const std::vector<Real>& u, Index start, Index window, Real factor) {
  for (Index k = start + window; k <= u.size(); ++k) {
    Real best = std::numeric_limits<Real>::infinity();
    for (Index i = start; i < k; ++i) best = std::min(best, u[i]);
    Real windowMin = std::numeric_limits<Real>::infinity();
    for (Index i = k - window; i < k; ++i) windowMin = std::min(windowMin, u[i]);
    if (windowMin >= factor * best) return k;
  }
  return 0;
}

void dumpHistory(const char* label, const SIMPLEResult& r, Index maxEntries) {
  std::printf("  %s: status %-13s iterations %zu  final u %.3e v %.3e p %.3e cont %.3e\n", label,
              statusName(r.status), static_cast<std::size_t>(r.iterations), r.finalUResidual,
              r.finalVResidual, r.finalPressureResidual, r.finalContinuityResidual);
  if (!r.robustness.statusDetail.empty()) {
    std::printf("      detail: %s\n", r.robustness.statusDetail.c_str());
  }
  std::printf("      u residual history:");
  const Index n = std::min<Index>(r.uResidualHistory.size(), maxEntries);
  for (Index k = 0; k < n; ++k) {
    if (k % 6 == 0) std::printf("\n        ");
    std::printf("[%3zu] %.4e  ", static_cast<std::size_t>(k), r.uResidualHistory[k]);
  }
  if (r.uResidualHistory.size() > n) std::printf("\n        ... (%zu more)",
                                                 static_cast<std::size_t>(
                                                     r.uResidualHistory.size() - n));
  std::printf("\n");
}

}  // namespace

int main() {
  std::printf("# A6 Step 1 -- M-E investigation. No test modified; no value adopted.\n\n");

  // -------------------------------------------------------------------------------------------
  // M-E test 1: DefaultRobustnessPreservesBaseline -- cavity(4, 0.01), alpha 0.7/0.3, tol 1e-6.
  // Intent: with the DEFAULT robustness settings, and with the detectors merely enabled on a case
  // that should not need them, the solver must be bit-identical to the pre-P12-NUM-004 solver.
  // -------------------------------------------------------------------------------------------
  std::printf("## M-E 1: DefaultRobustnessPreservesBaseline -- cavity 4x4, mu 0.01, a=0.7/0.3,"
              " tol 1e-6\n");
  {
    const Case c = cavity(4, 0.01);
    const SIMPLESettings base = cavitySettings(0.7, 0.3, 500, 1e-6);
    const SIMPLEResult reference = solve(c, base);
    dumpHistory("reference (no detectors)", reference, 40);

    SIMPLESettings guardedSettings = base;
    guardedSettings.robustness.stagnation.enabled = true;
    guardedSettings.robustness.divergence.enabled = true;
    guardedSettings.robustness.linearSolverFallback.enabled = true;
    guardedSettings.robustness.linearSolverFallback.maxAttempts = 3;
    const SIMPLEResult guarded = solve(c, guardedSettings);
    dumpHistory("guarded (detectors on)", guarded, 40);

    std::printf("      fallbacks used: %zu\n",
                static_cast<std::size_t>(guarded.robustness.linearSolverFallbacks));

    // Which detector, and is its condition genuinely met on the reference trajectory?
    const Index fires = detectorWouldFireAt(reference.uResidualHistory, 10, 10, 10.0);
    std::printf("      independent replay of the divergence condition on the REFERENCE history:"
                " fires at %zu (0 = never)\n", static_cast<std::size_t>(fires));
    if (fires != 0) {
      Real best = std::numeric_limits<Real>::infinity();
      for (Index i = 10; i < fires; ++i) best = std::min(best, reference.uResidualHistory[i]);
      Real windowMin = std::numeric_limits<Real>::infinity();
      for (Index i = fires - 10; i < fires; ++i) {
        windowMin = std::min(windowMin, reference.uResidualHistory[i]);
      }
      std::printf("      best since 10 = %.4e, window minimum = %.4e, ratio = %.3f (factor 10)\n",
                  best, windowMin, windowMin / best);
    }

    // Does the SOLUTION differ? Compare the guarded run's fields against the reference's.
    Real worstU = 0.0;
    Real worstV = 0.0;
    Real worstP = 0.0;
    const Index m = std::min<Index>(reference.velocity.size(), guarded.velocity.size());
    for (Index i = 0; i < m; ++i) {
      worstU = std::max(worstU, std::abs(reference.velocity[i].x - guarded.velocity[i].x));
      worstV = std::max(worstV, std::abs(reference.velocity[i].y - guarded.velocity[i].y));
      worstP = std::max(worstP, std::abs(reference.pressure[i] - guarded.pressure[i]));
    }
    std::printf("      reference-vs-guarded field difference: u %.3e  v %.3e  p %.3e\n", worstU,
                worstV, worstP);
    std::printf("      => the reference run itself is %s; the guarded run is %s\n",
                statusName(reference.status), statusName(guarded.status));
  }

  // -------------------------------------------------------------------------------------------
  // M-E test 2: DivergenceStatus -- cavity(8, 0.01), alpha 0.9/0.9, tol 1e-6, detector on.
  // Intent: a genuine finite runaway is stopped, with finite fields, at the earliest iteration the
  // detector can possibly fire (startIteration 10 + window 10 = 20).
  // -------------------------------------------------------------------------------------------
  std::printf("\n## M-E 2: DivergenceStatus -- cavity 8x8, mu 0.01, a=0.9/0.9, tol 1e-6,"
              " divergence on\n");
  {
    const Case c = cavity(8, 0.01);
    SIMPLESettings settings = cavitySettings(0.9, 0.9, 400, 1e-6);
    settings.robustness.divergence.enabled = true;
    const SIMPLEResult r = solve(c, settings);
    dumpHistory("diverging run", r, 25);
    std::printf("      growth: final u / first u = %.3e  (test requires > 1e3)\n",
                r.finalUResidual / r.uResidualHistory.front());
    std::printf("      all finite: %s\n",
                (std::isfinite(r.finalUResidual) && std::isfinite(r.finalContinuityResidual))
                    ? "yes" : "NO");
    // Without the detector, how far does it actually run? That is what the detector is protecting.
    SIMPLESettings undetected = cavitySettings(0.9, 0.9, 400, 1e-6);
    const SIMPLEResult u = solve(c, undetected);
    std::printf("      same case WITHOUT the detector: status %s after %zu iterations,"
                " final u %.3e\n",
                statusName(u.status), static_cast<std::size_t>(u.iterations), u.finalUResidual);
    const Index fires = detectorWouldFireAt(u.uResidualHistory, 10, 10, 10.0);
    std::printf("      independent replay of the condition on the undetected history:"
                " fires at %zu\n", static_cast<std::size_t>(fires));
  }

  // -------------------------------------------------------------------------------------------
  // Sensitivity of the recorded counts: tolerance, and mesh size.
  // -------------------------------------------------------------------------------------------
  std::printf("\n## sensitivity of the recorded iteration counts\n");
  std::printf("%-34s %-14s %-12s %s\n", "configuration", "status", "iterations", "final u");
  for (const Real tol : {1e-5, 1e-6, 1e-7, 1e-8}) {
    const Case c = cavity(4, 0.01);
    const SIMPLEResult r = solve(c, cavitySettings(0.7, 0.3, 500, tol));
    std::printf("cavity 4x4 a=0.7/0.3 tol=%.0e      %-14s %-12zu %.3e\n", tol,
                statusName(r.status), static_cast<std::size_t>(r.iterations), r.finalUResidual);
  }
  for (const Index n : {4, 6, 8}) {
    const Case c = cavity(n, 0.01);
    SIMPLESettings s = cavitySettings(0.9, 0.9, 400, 1e-6);
    s.robustness.divergence.enabled = true;
    const SIMPLEResult r = solve(c, s);
    std::printf("cavity %dx%d a=0.9/0.9 divergence on   %-14s %-12zu %.3e\n", static_cast<int>(n),
                static_cast<int>(n), statusName(r.status), static_cast<std::size_t>(r.iterations),
                r.finalUResidual);
  }
  return 0;
}

// RESUMED A6 Step 1B: what does the divergence detector mathematically guarantee about its trigger
// time? Investigation only -- no test is modified and no observed value is adopted as an expectation.
//
// DERIVATION, from src/solver/SolverRobustness.cpp (read-only).
//
// `record()` evaluates, in this order:
//   1. isConverged           -> Converged
//   2. a NON-FINITE residual norm, with divergence enabled -> Diverging, at ANY iteration
//      (SolverRobustness.cpp:471-480). This path has no window, so it is the one exception to the
//      windowed lower bound below. It cannot apply to a test that also asserts finite fields.
//   3. the windowed growth rules, but only when
//           n >= max(startIteration, 1) + window                        (line 481-482)
//      so for a run whose residual norms stay finite:
//
//          EARLIEST POSSIBLE TRIGGER  =  max(startIteration, 1) + window
//
//      This is a hard structural bound, independent of the mesh, the physics and the trajectory.
//      With the defaults (startIteration 10, window 10) it is exactly 20 -- which is where the
//      historical `EXPECT_EQ(iterations, 20u)` came from. It is a LOWER bound, not a prediction.
//
//   Within the windowed block, two rules can fire (`diverging`):
//     RULE 1, persistent excursion: min(last `window` values) >= growth * max(floor, bestBeforeWindow)
//     RULE 2, sustained runaway:    the last `window` values increase strictly at every step
//                                   AND newest >= growth * max(floor, oldest)
//
//   GUARANTEED TRIGGER. No universal finite upper bound exists -- a residual can hover just under
//   the growth factor forever. But one is derivable for a run that actually runs away, which is what
//   this test asserts (its own bound is final/first >= 1e3):
//
//     let b = min of the values in [start, start + window).
//     If every value from iteration start + window onward is >= growth * b, then by
//     n = start + 2*window the whole window lies above growth * b and RULE 1 fires. Hence
//
//          GUARANTEED TRIGGER  <=  max(startIteration, 1) + 2 * window
//
//     for any trajectory that reaches and holds `growth` times its early-window minimum within
//     `window` iterations. That is a property of the rule, not of a measurement.
//
// This probe measures the OBSERVED trigger across settings and meshes and checks it against both
// derived bounds, and records which rule fired.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

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

std::size_t lowerViolations = 0;
std::size_t upperViolations = 0;
std::size_t notDetected = 0;

void probe(Index n, Index start, Index window, Real growth) {
  const Case c = cavity(n, 0.01);
  SIMPLESettings s;
  s.maxIterations = 400;
  s.velocityRelaxation = 0.9;
  s.pressureRelaxation = 0.9;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-6;
  s.continuityTolerance = 1e-6;
  s.robustness.divergence.enabled = true;
  s.robustness.divergence.startIteration = start;
  s.robustness.divergence.window = window;
  s.robustness.divergence.growthFactor = growth;
  const SIMPLE simple(s, 0);
  const SIMPLEResult r =
      simple.solve(c.mesh, c.fluid, c.velocity, c.pressure,
                   fields::VectorField(c.mesh.numberOfCells(), Vector2{0.0, 0.0}),
                   fields::ScalarField(c.mesh.numberOfCells(), 0.0));

  const Index earliest = std::max<Index>(start, 1) + window;
  const Index guaranteed = std::max<Index>(start, 1) + 2 * window;
  const bool caught = r.status == SIMPLEStatus::Diverging;
  const bool finite = std::isfinite(r.finalUResidual);
  const Real growthSeen = r.finalUResidual / r.uResidualHistory.front();
  const char* rule = "none";
  if (r.robustness.statusDetail.find("every one of the last") != std::string::npos) {
    rule = "RULE 1 persistent excursion";
  } else if (r.robustness.statusDetail.find("increased at each of the last") != std::string::npos) {
    rule = "RULE 2 sustained runaway";
  } else if (r.robustness.statusDetail.find("overflowed") != std::string::npos) {
    rule = "non-finite overflow";
  }

  if (!caught) ++notDetected;
  if (caught && r.iterations < earliest) ++lowerViolations;
  if (caught && r.iterations > guaranteed) ++upperViolations;

  std::printf("  %2lldx%-2lld start %2lld window %2lld growth %5.1f | earliest %2lld guaranteed %2lld"
              " | observed %3zu | %-27s | growth %.2e finite %s | %s%s\n",
              static_cast<long long>(n), static_cast<long long>(n), static_cast<long long>(start),
              static_cast<long long>(window), growth, static_cast<long long>(earliest),
              static_cast<long long>(guaranteed), static_cast<std::size_t>(r.iterations), rule,
              growthSeen, finite ? "yes" : "NO", caught ? "Diverging" : "*** NOT DETECTED ***",
              (caught && (r.iterations < earliest || r.iterations > guaranteed))
                  ? "  *** OUTSIDE THE DERIVED BOUNDS ***"
                  : "");
}

}  // namespace

int main() {
  std::printf("# RESUMED A6 Step 1B -- divergence trigger time: derived bounds vs observation.\n");
  std::printf("# earliest possible  = max(startIteration,1) + window        (structural)\n");
  std::printf("# guaranteed trigger = max(startIteration,1) + 2*window      (for a real runaway)\n");
  std::printf("# The historical assertion `iterations == 20` is the earliest-possible value for the\n");
  std::printf("# default settings, asserted as if it were a prediction.\n\n");

  std::printf("## the test's own settings (start 10, window 10, growth 10), across meshes\n");
  for (const Index n : {4, 6, 8, 12, 16}) probe(n, 10, 10, 10.0);

  std::printf("\n## window swept, the test's mesh (8x8)\n");
  for (const Index w : {2, 5, 10, 20}) probe(8, 10, w, 10.0);

  std::printf("\n## startIteration swept (8x8, window 10)\n");
  for (const Index st : {1, 5, 10, 20, 30}) probe(8, st, 10, 10.0);

  std::printf("\n## growthFactor swept (8x8, start 10, window 10)\n");
  for (const Real g : {2.0, 10.0, 100.0, 1000.0}) probe(8, 10, 10, g);

  std::printf("\n## totals over %s configurations\n", "all");
  std::printf("  runaways not detected                      %zu   (must be 0)\n", notDetected);
  std::printf("  triggers EARLIER than mathematically possible %zu   (must be 0)\n", lowerViolations);
  std::printf("  triggers later than the derived guarantee  %zu   (must be 0)\n", upperViolations);
  std::printf("\nRESULT: %s\n",
              (notDetected == 0 && lowerViolations == 0 && upperViolations == 0) ? "both derived"
              " bounds hold in every configuration" : "a derived bound was violated");
  return 0;
}

// RESUMED A6 Step 4: non-vacuity of the criteria A6 proposes, BEFORE the gate is frozen.
//
// A policy test that merely accepts the current production result is invalid. So for the proposed
// DivergenceStatus criterion
//
//     status == Diverging
//     && iterations >= max(startIteration, 1) + window              (earliest possible)
//     && iterations <= max(startIteration, 1) + 2 * window          (derived guarantee)
//     && finite fields && growth >= 1e3 && detail names divergence
//
// this probe drives three deliberately degraded configurations and checks that the criterion
// REJECTS each one:
//
//   D1  detector disabled                  -> no Diverging status at all
//   D2  a trigger earlier than the earliest possible for the asserted settings
//   D3  a genuine runaway that is not classified Diverging (adaptive relaxation recovers it)
//
// Investigation only: nothing is modified, and the criterion is evaluated here in probe code.
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

SIMPLEResult solve(const Case& c, const SIMPLESettings& s) {
  const SIMPLE simple(s, 0);
  return simple.solve(c.mesh, c.fluid, c.velocity, c.pressure,
                      fields::VectorField(c.mesh.numberOfCells(), Vector2{0.0, 0.0}),
                      fields::ScalarField(c.mesh.numberOfCells(), 0.0));
}

// The PROPOSED criterion, evaluated against the settings that are asserted (start 10, window 10 --
// the defaults DivergenceStatus uses), independent of what the run was actually configured with.
constexpr Index kAssertedStart = 10;
constexpr Index kAssertedWindow = 10;

struct Verdict {
  bool statusOk{};
  bool lowerOk{};
  bool upperOk{};
  bool finiteOk{};
  bool growthOk{};
  bool detailOk{};
  bool all() const { return statusOk && lowerOk && upperOk && finiteOk && growthOk && detailOk; }
};

Verdict evaluate(const SIMPLEResult& r) {
  const Index earliest = std::max<Index>(kAssertedStart, 1) + kAssertedWindow;
  const Index guaranteed = std::max<Index>(kAssertedStart, 1) + 2 * kAssertedWindow;
  Verdict v;
  v.statusOk = r.status == SIMPLEStatus::Diverging;
  v.lowerOk = r.iterations >= earliest;
  v.upperOk = r.iterations <= guaranteed;
  v.finiteOk = std::isfinite(r.finalUResidual) && std::isfinite(r.finalContinuityResidual);
  v.growthOk = !r.uResidualHistory.empty() &&
               r.finalUResidual > 1e3 * r.uResidualHistory.front();
  v.detailOk = r.robustness.statusDetail.find("divergence") != std::string::npos;
  return v;
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

std::size_t wronglyAccepted = 0;

void control(const char* label, const SIMPLEResult& r, bool shouldBeAccepted) {
  const Verdict v = evaluate(r);
  std::printf("  %-44s %-13s %3zu it | status %d lower %d upper %d finite %d growth %d detail %d"
              " -> %s  %s\n",
              label, statusName(r.status), static_cast<std::size_t>(r.iterations), v.statusOk,
              v.lowerOk, v.upperOk, v.finiteOk, v.growthOk, v.detailOk,
              v.all() ? "ACCEPTED" : "REJECTED",
              (v.all() == shouldBeAccepted) ? "(as required)" : "*** WRONG ***");
  if (v.all() != shouldBeAccepted) ++wronglyAccepted;
}

}  // namespace

int main() {
  std::printf("# RESUMED A6 Step 4 -- non-vacuity of the proposed DivergenceStatus criterion.\n");
  std::printf("# The criterion is evaluated with the asserted settings start=%lld window=%lld,\n",
              static_cast<long long>(kAssertedStart), static_cast<long long>(kAssertedWindow));
  std::printf("# so earliest possible = 20 and the derived guarantee = 30.\n\n");

  const Case c = cavity(8, 0.01);
  SIMPLESettings base;
  base.maxIterations = 400;
  base.velocityRelaxation = 0.9;
  base.pressureRelaxation = 0.9;
  base.velocityTolerance = 1e-6;
  base.pressureTolerance = 1e-6;
  base.continuityTolerance = 1e-6;

  std::printf("## the genuine case the test protects -- must be ACCEPTED\n");
  {
    SIMPLESettings s = base;
    s.robustness.divergence.enabled = true;
    control("P  genuine runaway, detector on", solve(c, s), true);
  }

  std::printf("\n## deliberately degraded configurations -- each must be REJECTED\n");
  {
    // D1: the detector is switched off. Nothing stops the runaway.
    SIMPLESettings s = base;
    s.robustness.divergence.enabled = false;
    control("D1 detector DISABLED", solve(c, s), false);
  }
  {
    // D2: a detector that fires earlier than the asserted settings allow. Configured with
    // startIteration 1, so it can legitimately fire at 11 -- but the criterion is asserted for
    // start 10 / window 10, whose earliest possible trigger is 20. A detector that stopped at 11
    // while claiming those settings would be firing before it mathematically could.
    SIMPLESettings s = base;
    s.robustness.divergence.enabled = true;
    s.robustness.divergence.startIteration = 1;
    control("D2 fires before mathematically possible", solve(c, s), false);
  }
  {
    // D3: the same runaway, recovered by adaptive relaxation -> Converged, not Diverging.
    SIMPLESettings s = base;
    s.maxIterations = 3000;
    s.robustness.divergence.enabled = true;
    s.robustness.adaptiveRelaxation.enabled = true;
    s.robustness.adaptiveRelaxation.minVelocity = 0.2;
    s.robustness.adaptiveRelaxation.maxVelocity = 0.95;
    s.robustness.adaptiveRelaxation.minPressure = 0.05;
    s.robustness.adaptiveRelaxation.maxPressure = 0.95;
    control("D3 runaway recovered, not Diverging", solve(c, s), false);
  }
  {
    // D4: a healthy converging case must not satisfy a divergence criterion either.
    SIMPLESettings s = base;
    s.velocityRelaxation = 0.7;
    s.pressureRelaxation = 0.3;
    s.maxIterations = 3000;
    s.robustness.divergence.enabled = true;
    control("D4 healthy converging run", solve(c, s), false);
  }

  std::printf("\n## totals\n");
  std::printf("  controls with the wrong verdict: %zu   (Step 4 requires 0)\n", wronglyAccepted);
  std::printf("\nRESULT: %s\n", wronglyAccepted == 0
                                    ? "the proposed criterion rejects every degraded configuration"
                                    : "the proposed criterion is VACUOUS for some control");
  return wronglyAccepted == 0 ? 0 : 1;
}

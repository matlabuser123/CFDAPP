// P12-DIFF-002-UC-001 Step 6/8: measure the Poiseuille case's dp/dx and centreline on the
// current grid triplet AND on the next refinement, so the pressure-gradient order gate can be
// designed on evidence rather than on the coarsest grid that happens to be in the suite.
//
// Same case and same SIMPLE settings as test_poiseuille_production_validation.cpp (L = 8H,
// H = 1, rho = 1, mu = 0.1, uniform inlet U = 1, zero-gradient outlet with p = 0), and the same
// pair-averaged pressure estimator. Investigation only: no test and no production file changes.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "PoiseuilleValidationUtils.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;

namespace {

constexpr Real kHeight = 1.0;
constexpr Real kLength = 8.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kProfileStation = 0.75 * kLength;
constexpr Real kPressureStation1 = 0.40 * kLength;
constexpr Real kPressureStation2 = 0.75 * kLength;

cfd::pressure_velocity::SIMPLESettings settings() {
  cfd::pressure_velocity::SIMPLESettings s;
  s.maxIterations = 8000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5;
  s.pressureTolerance = 5e-4;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 5000;
  s.pressureSolver.absoluteTolerance = 1e-10;
  s.pressureSolver.relativeTolerance = 1e-8;
  s.pressureSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  s.robustness.linearSolverFallback.enabled = true;
  return s;
}

// The INDEPENDENTLY derived DIFF-002 closed forms (uc001_reference.py), written from the
// derivation, not read from CFDApp.
Real referenceDpdx(Index ny) {
  const Real n2 = static_cast<Real>(ny) * static_cast<Real>(ny);
  return -12.0 * kViscosity * kMeanVelocity / (kHeight * kHeight) * (2.0 * n2) / (2.0 * n2 + 1.0);
}
Real referenceVelocity(Real y, Index ny) {
  // DIFF-002's discrete solution equals the continuum profile at the cell centres, with the
  // flow-rate-consistent gradient G = dp/dx / mu.
  const Real g = referenceDpdx(ny) / kViscosity;
  return -0.5 * g * y * (kHeight - y);
}

struct GridResult {
  Index nx{0};
  Index ny{0};
  bool accepted{false};
  Real dpdx{0.0};
  Real centreline{0.0};
  Real oddEven{0.0};
  Real profileLinfVsReference{0.0};
  Real seconds{0.0};
};

GridResult run(Index nx, Index ny) {
  using cfd::boundary::BoundaryConditionSet;
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(nx, ny, kLength, kHeight);
  BoundaryConditionSet velocity;
  velocity.set(mesh, "left", std::make_unique<cfd::boundary::Inlet>(Vector2{kMeanVelocity, 0.0}));
  velocity.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  velocity.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  velocity.set(mesh, "top", std::make_unique<cfd::boundary::Wall>());
  BoundaryConditionSet pressure;
  pressure.set(mesh, "left", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  pressure.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
  pressure.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  pressure.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  const cfd::pressure_velocity::SIMPLE simple(settings(), /*referenceCell=*/0);
  const auto start = std::chrono::steady_clock::now();
  const auto result =
      simple.solve(mesh, cfd::physics::FluidProperties(kDensity, kViscosity), velocity, pressure,
                   cfd::fields::VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                   cfd::fields::ScalarField(mesh.numberOfCells(), 0.0));

  GridResult g;
  g.nx = nx;
  g.ny = ny;
  g.accepted = result.converged();
  g.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  const auto profile = cfd::validation::extractVerticalProfileU(mesh, nx, ny, result.velocity,
                                                                kProfileStation, kHeight);
  g.centreline = cfd::validation::interpolateProfile(profile, 0.5 * kHeight);
  for (const auto& s : profile) {
    if (s.coordinate <= 0.0 || s.coordinate >= kHeight) continue;
    g.profileLinfVsReference =
        std::max(g.profileLinfVsReference, std::abs(s.value - referenceVelocity(s.coordinate, ny)));
  }
  const auto grad = cfd::validation::pairAveragedPressureGradient(
      mesh, nx, ny, result.pressure, kPressureStation1, kPressureStation2);
  g.dpdx = grad.gradient;
  g.oddEven = cfd::validation::pressureOddEvenAmplitude(mesh, nx, ny, result.pressure,
                                                        kPressureStation1, kPressureStation2);
  return g;
}

// The project's own three-grid statistics (src/validation/GridConvergence.cpp), constant r.
void order(const char* label, Real coarse, Real medium, Real fine, Real r = 1.5,
           Real formal = 2.0) {
  const Real e21 = medium - fine;
  const Real e32 = coarse - medium;
  const Real ratio = e32 / e21;
  if (ratio <= 1.0) {
    std::printf("  %-38s not monotonic/convergent (eps32/eps21 = %.4f)\n", label, ratio);
    return;
  }
  const Real p = std::log(ratio) / std::log(r);
  const Real rf = std::pow(r, formal);
  const Real u21 = std::abs(e21) / (rf - 1.0);
  const Real u32 = std::abs(e32) / (rf - 1.0);
  const Real asym = u32 / (rf * u21);
  std::printf("  %-38s p = %7.4f   asymptotic ratio = %7.4f   %s\n", label, p, asym,
              std::abs(asym - 1.0) <= 0.1 ? "asymptotic" : "monotonic_not_asymptotic");
}

}  // namespace

int main() {
  std::printf("# UC-001 Poiseuille grid sweep -- CFDApp PRODUCTION RESULT vs the independently\n");
  std::printf("# derived DIFF-002 reference dp/dx = -1.2 * 2ny^2/(2ny^2+1)\n\n");
  std::vector<GridResult> results;
  for (const auto& [nx, ny] : std::vector<std::pair<Index, Index>>{
           {64, 8}, {96, 12}, {144, 18}, {216, 27}}) {
    results.push_back(run(nx, ny));
    const auto& g = results.back();
    const Real ref = referenceDpdx(g.ny);
    std::printf("  %3lldx%-3lld conv %-3s  dp/dx %.15f  reference %.15f\n",
                static_cast<long long>(g.nx), static_cast<long long>(g.ny),
                g.accepted ? "yes" : "NO", g.dpdx, ref);
    std::printf("            extraction offset %+.6e   odd-even amplitude %.6e\n",
                g.dpdx - ref, g.oddEven);
    std::printf("            centreline %.15f   profile Linf vs reference %.6e   %.1f s\n",
                g.centreline, g.profileLinfVsReference, g.seconds);
  }

  std::printf("\n## dp/dx observed order, production measurements\n");
  order("current triplet 8/12/18", results[0].dpdx, results[1].dpdx, results[2].dpdx);
  order("refined triplet 12/18/27", results[1].dpdx, results[2].dpdx, results[3].dpdx);
  std::printf("\n## dp/dx observed order, independent reference (no CFDApp)\n");
  order("current triplet 8/12/18", referenceDpdx(8), referenceDpdx(12), referenceDpdx(18));
  order("refined triplet 12/18/27", referenceDpdx(12), referenceDpdx(18), referenceDpdx(27));

  std::printf("\n## centreline observed order, production measurements\n");
  order("current triplet 8/12/18", results[0].centreline, results[1].centreline,
        results[2].centreline);
  order("refined triplet 12/18/27", results[1].centreline, results[2].centreline,
        results[3].centreline);
  return 0;
}

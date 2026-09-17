// P12-DIFF-002-INV-002 / INV-F4: low-Mach global mass imbalance.
//
// The failing metric (tests/integration/compressible/test_low_mach_regression.cpp:259) is
//     |globalNetFlux(compressible mass flux)| / (rhoAvg * U * H)
// on a 32x6 channel, where the velocity/pressure field comes from an INCOMPRESSIBLE SIMPLE solve
// that stops on its own tolerances (velocity 2e-5, pressure 5e-4, continuity 1e-6). So the number
// is a property of where SIMPLE stopped as much as of the spatial discretization. This probe
// transcribes the test's configuration and sweeps the stopping tolerances, to separate
//   discretization error  from  incomplete iterative convergence.
// Compiled against the current library and, separately, against the isolated pre-DIFF-002 baseline
// library, so the same code produces both sides of the comparison.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using namespace cfd;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

constexpr Real kChannelHeight = 1.0;
constexpr Real kChannelLength = 8.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kMeanVelocity = 1.0;
constexpr Real kGasConstant = 287.05;
constexpr Real kTemperature = 300.0;
constexpr Real kReferencePressure = 101325.0;

boundary::BoundaryConditionSet velocityBcs(const Mesh& mesh) {
  boundary::BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<boundary::Inlet>(Vector3{kMeanVelocity, 0.0, 0.0}));
  b.set(mesh, "right", std::make_unique<boundary::Outlet>());
  b.set(mesh, "bottom", std::make_unique<boundary::Wall>());
  b.set(mesh, "top", std::make_unique<boundary::Wall>());
  return b;
}

boundary::BoundaryConditionSet pressureBcs(const Mesh& mesh) {
  boundary::BoundaryConditionSet b;
  b.set(mesh, "left", std::make_unique<boundary::FixedGradient>(0.0));
  b.set(mesh, "right", std::make_unique<boundary::FixedValue>(0.0));
  b.set(mesh, "bottom", std::make_unique<boundary::FixedGradient>(0.0));
  b.set(mesh, "top", std::make_unique<boundary::FixedGradient>(0.0));
  return b;
}

pressure_velocity::SIMPLESettings settings(Real velTol, Real pTol, Real contTol) {
  pressure_velocity::SIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = velTol;
  s.pressureTolerance = pTol;
  s.continuityTolerance = contTol;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-8;
  s.pressureSolver.relativeTolerance = 1e-6;
  return s;
}

void run(const std::string& label, Index nx, Index ny, Real velTol, Real pTol, Real contTol) {
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, kChannelLength, kChannelHeight);
  const auto vb = velocityBcs(mesh);
  const auto pb = pressureBcs(mesh);
  const physics::FluidProperties fluid(kDensity, kViscosity);
  fields::VectorField velocity(mesh.numberOfCells(), Vector3{kMeanVelocity, 0.0, 0.0});
  fields::ScalarField pressure(mesh.numberOfCells(), 0.0);
  const auto flow = pressure_velocity::SIMPLE(settings(velTol, pTol, contTol))
                        .solve(mesh, fluid, vb, pb, velocity, pressure);

  const Index n = mesh.numberOfCells();
  const compressible::ThermodynamicProperties thermo(kGasConstant, 1005.0);
  fields::ScalarField density(n);
  Real rhoMin = 0.0, rhoMax = 0.0;
  for (Index i = 0; i < n; ++i) {
    density[i] = thermo.density(kReferencePressure + flow.pressure[i], kTemperature);
    rhoMin = (i == 0) ? density[i] : std::min(rhoMin, density[i]);
    rhoMax = (i == 0) ? density[i] : std::max(rhoMax, density[i]);
  }
  const Real rhoAvg = 0.5 * (rhoMin + rhoMax);
  // The test's own construction: an EOS-evaluated per-cell AND per-boundary-face density
  // (P12-COMP-001), not a uniform rhoAvg.
  const fields::ScalarField temperature(n, kTemperature);
  const fields::SurfaceField compressibleFlux = compressible::calculateCompressibleMassFlux(
      mesh, flow.velocity, density, vb, flow.pressure, pb, kReferencePressure, thermo, temperature,
      nullptr);
  const auto continuity = physics::evaluateContinuity(mesh, compressibleFlux);
  const Real metric = std::abs(continuity.globalNetFlux) /
                      std::max<Real>(1e-9, rhoAvg * kMeanVelocity * kChannelHeight);

  std::printf("INV-F4 %-9s %3zux%-2zu velTol %.0e pTol %.0e contTol %.0e | status %d it %5zu |"
              " metric %.6e (bound 1e-4 %s) | |globalNetFlux| %.6e  rhoAvg %.6f |"
              " SIMPLE massImb %.3e\n",
              label.c_str(), static_cast<std::size_t>(nx), static_cast<std::size_t>(ny), velTol,
              pTol, contTol, static_cast<int>(flow.status),
              static_cast<std::size_t>(flow.iterations), metric,
              (metric < 1e-4) ? "PASS" : "FAIL", std::abs(continuity.globalNetFlux), rhoAvg,
              flow.globalMassImbalance);
}

}  // namespace

int main() {
  std::printf("# INV-F4 low-Mach global mass imbalance, 32x6 (the failing configuration).\n");
  std::printf("# The field comes from an incompressible SIMPLE solve that stops on its own\n");
  std::printf("# tolerances, so the metric is swept against them to separate discretization\n");
  std::printf("# error from incomplete iterative convergence. Authoritative defaults unchanged.\n\n");
  std::printf("## the test's own settings, repeated 3x for determinism\n");
  for (int i = 0; i < 3; ++i) run("as-tested", 32, 6, 2e-5, 5e-4, 1e-6);
  std::printf("\n## tolerance sweep (tighter stopping criteria only)\n");
  run("tighter", 32, 6, 2e-6, 5e-5, 1e-7);
  run("tighter", 32, 6, 2e-7, 5e-6, 1e-8);
  run("tighter", 32, 6, 2e-8, 5e-7, 1e-9);
  run("tighter", 32, 6, 2e-9, 5e-8, 1e-10);
  std::printf("\n## mesh refinement at the test's own tolerances\n");
  run("refine", 64, 12, 2e-5, 5e-4, 1e-6);
  run("refine", 128, 24, 2e-5, 5e-4, 1e-6);
  return 0;
}

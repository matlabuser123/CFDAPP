// P12-DIFF-002-LOWMACH-001: what does LowMachRegressionTest.GlobalMassImbalanceIsSmall measure?
//
// runLowMachCase() is copied VERBATIM from tests/integration/compressible/test_low_mach_regression.cpp,
// with one switch: which velocity field feeds calculateCompressibleMassFlux and the Mach number.
//   "as-tested" : the test's local `velocity` -- the UNIFORM initial field (SIMPLE::solve takes its
//                 initial velocity BY VALUE, so the local is never updated)
//   "solved"    : flow.velocity, the solution the test's comments describe
// Also printed: the independent quantities the metric is compared with --
//   * dp_b/p_avg : (inlet-face gauge pressure - outlet gauge pressure) / average absolute pressure
//   * the boundary-inclusive density range (cells, and the EOS density of every boundary face)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleContinuity.hpp"
#include "cfd/compressible/CompressibleMassFlux.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using namespace cfd;
using cfd::boundary::BoundaryConditionSet;

namespace {
constexpr Real kChannelHeight = 1.0, kChannelLength = 8.0, kDensity = 1.0, kViscosity = 0.1,
               kMeanVelocity = 1.0;
constexpr Real kGasConstant = 287.05, kSpecificHeatPressure = 1005.0, kTemperature = 300.0,
               kReferencePressure = 101325.0;

BoundaryConditionSet vb(const mesh::Mesh& m) {
  BoundaryConditionSet b;
  b.set(m, "left", std::make_unique<boundary::Inlet>(Vector2{kMeanVelocity, 0.0}));
  b.set(m, "right", std::make_unique<boundary::Outlet>());
  b.set(m, "bottom", std::make_unique<boundary::Wall>());
  b.set(m, "top", std::make_unique<boundary::Wall>());
  return b;
}
BoundaryConditionSet pb(const mesh::Mesh& m) {
  BoundaryConditionSet b;
  b.set(m, "left", std::make_unique<boundary::FixedGradient>(0.0));
  b.set(m, "right", std::make_unique<boundary::FixedValue>(0.0));
  b.set(m, "bottom", std::make_unique<boundary::FixedGradient>(0.0));
  b.set(m, "top", std::make_unique<boundary::FixedGradient>(0.0));
  return b;
}
pressure_velocity::SIMPLESettings settings() {
  pressure_velocity::SIMPLESettings s;
  s.maxIterations = 3000;
  s.velocityRelaxation = 0.7;
  s.pressureRelaxation = 0.3;
  s.velocityTolerance = 2e-5;
  s.pressureTolerance = 5e-4;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.maxIterations = 500;
  s.momentumSolver.absoluteTolerance = 1e-10;
  s.momentumSolver.relativeTolerance = 1e-8;
  s.pressureSolver.maxIterations = 2000;
  s.pressureSolver.absoluteTolerance = 1e-8;
  s.pressureSolver.relativeTolerance = 1e-6;
  return s;
}

void run(Index nx, Index ny, bool solvedVelocity) {
  const mesh::Mesh mesh = mesh::MeshGeometry::createCartesian2D(nx, ny, kChannelLength, kChannelHeight);
  const auto velocityBoundaries = vb(mesh);
  const auto pressureBoundaries = pb(mesh);
  const physics::FluidProperties fluid(kDensity, kViscosity);
  const pressure_velocity::SIMPLE simple(settings());
  fields::VectorField velocity(mesh.numberOfCells(), Vector2{kMeanVelocity, 0.0});
  fields::ScalarField pressure(mesh.numberOfCells(), 0.0);
  const auto flow = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
  const fields::VectorField& u = solvedVelocity ? flow.velocity : velocity;

  const compressible::ThermodynamicProperties thermo(kGasConstant, kSpecificHeatPressure);
  const Index n = mesh.numberOfCells();
  fields::ScalarField density(n);
  for (Index i = 0; i < n; ++i) density[i] = thermo.density(kReferencePressure + flow.pressure[i], kTemperature);
  Real rhoMin = density[0], rhoMax = density[0];
  for (Index i = 1; i < n; ++i) { rhoMin = std::min(rhoMin, density[i]); rhoMax = std::max(rhoMax, density[i]); }
  const Real rhoAvg = 0.5 * (rhoMin + rhoMax);
  Real machMax = 0.0;
  for (Index i = 0; i < n; ++i) machMax = std::max(machMax, compressible::machNumber(magnitude(u[i]), thermo.speedOfSound(kTemperature)));
  const fields::ScalarField temperature(n, kTemperature);
  const auto cflux = compressible::calculateCompressibleMassFlux(mesh, u, density, velocityBoundaries, flow.pressure,
                                                                 pressureBoundaries, kReferencePressure, thermo, temperature, nullptr);
  const Real fluxScale = std::max<Real>(1e-9, rhoAvg * kMeanVelocity * kChannelHeight);
  Real maxRel = 0.0;
  for (Index f = 0; f < mesh.numberOfFaces(); ++f)
    maxRel = std::max(maxRel, std::abs(cflux[f] - rhoAvg * flow.massFlux[f]) / fluxScale);
  const auto continuity = physics::evaluateContinuity(mesh, cflux);
  const Real metric = std::abs(continuity.globalNetFlux) / fluxScale;
  // independent comparison quantities
  Real pIn = 0.0, pOut = 0.0, wIn = 0.0, wOut = 0.0;
  for (const Index f : mesh.boundaryPatch("left").faceIds()) { pIn += flow.pressure[mesh.face(f).owner()] * mesh.face(f).area(); wIn += mesh.face(f).area(); }
  for (const Index f : mesh.boundaryPatch("right").faceIds()) { (void)f; wOut += mesh.face(f).area(); }
  pIn /= wIn;  // FixedGradient(0): the inlet face pressure is its owner's
  pOut = 0.0;  // FixedValue(0)
  const Real dpOverP = (pIn - pOut) / (kReferencePressure + 0.5 * (pIn + pOut));
  const Real rhoInFace = thermo.density(kReferencePressure + pIn, kTemperature);
  const Real rhoOutFace = thermo.density(kReferencePressure, kTemperature);
  const Real bRange = (std::max({rhoMax, rhoInFace, rhoOutFace}) - std::min({rhoMin, rhoInFace, rhoOutFace})) / rhoAvg;
  // Derived bound (candidate criterion): with F'_f the volumetric boundary fluxes of the same
  // velocity field (calculateMassFlux, rho = 1) and every boundary-face density inside
  // [rhoLo, rhoHi] (EOS at the BC-implied pressures: owner cells for the zero-gradient patches,
  // p_ref for the outlet), |sum rho_f F'_f| <= (rhoHi - rhoLo)/2 * sum|F'_f| + rhoMid |sum F'_f|.
  const auto vflux = physics::calculateMassFlux(mesh, u, physics::FluidProperties(1.0, kViscosity),
                                                velocityBoundaries);
  const Real rhoHi = std::max(rhoMax, rhoOutFace), rhoLo = std::min(rhoMin, rhoOutFace);
  Real sumAbs = 0.0, eps = 0.0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) continue;
    sumAbs += std::abs(vflux[face.id()]);
    eps += vflux[face.id()];
  }
  const Real bound = (0.5 * (rhoHi - rhoLo) * sumAbs + 0.5 * (rhoHi + rhoLo) * std::abs(eps)) / fluxScale;
  std::printf("  %4lldx%-3lld %-9s bound %.6e (metric/bound %.4f) | recomputed volumetric |eps'|/UH %.3e |"
              " sum|F'|/UH %.4f\n", (long long)nx, (long long)ny, solvedVelocity ? "solved" : "as-tested",
              bound, metric / bound, std::abs(eps) / (kMeanVelocity * kChannelHeight),
              sumAbs / (kMeanVelocity * kChannelHeight));
  std::printf("  %4lldx%-3lld %-9s status %d it %5lld | metric %.6e | dp_b/p_avg %.6e | cell drho/rho %.6e |"
              " boundary-incl drho/rho %.6e | SIMPLE |net|/UH %.3e | maxRelFluxDiff %.4e | Mach max %.4e\n",
              (long long)nx, (long long)ny, solvedVelocity ? "solved" : "as-tested", (int)flow.status,
              (long long)flow.iterations, metric, dpOverP, (rhoMax - rhoMin) / rhoAvg, bRange,
              flow.globalMassImbalance / (kMeanVelocity * kChannelHeight), maxRel, machMax);
}
}  // namespace

int main() {
  std::printf("# LOWMACH-001: the test's runLowMachCase, verbatim, with the velocity source switched\n");
  for (auto [nx, ny] : {std::pair<Index, Index>{16, 6}, {32, 6}, {64, 6}, {64, 12}, {128, 24}}) {
    run(nx, ny, false);
    run(nx, ny, true);
  }
  return 0;
}

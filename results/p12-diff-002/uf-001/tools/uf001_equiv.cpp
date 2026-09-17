// P12-DIFF-002-UF-001.3: verify that the problem CFDApp solves is mathematically the de Vahl
// Davis benchmark problem, rather than asserting it.
//
// Derivation (see summary.md UF-001.3). With rho=1, mu=Pr (so nu=Pr), k=cp=1 (so alpha=1), L=1,
// T_hot=1, T_cold=0, |g|=1 in -y, beta=Ra*Pr, BoussinesqBuoyancy::source returns
//     f = -rho*beta*(T - T_ref)*g = (0, Ra*Pr*(T - T_ref))
// so CFDApp solves
//     (u.grad)u = -grad p + Pr lap u + (0, Ra*Pr*(T - 0.5)),      (u.grad)T = lap T
// while the benchmark is
//     (U.grad)U = -grad P + Pr lap U + (0, Ra*Pr*theta),          (U.grad)theta = lap theta
// with theta = (T - T_cold)/(T_hot - T_cold) = T. The two momentum sources differ by the
// CONSTANT -Ra*Pr*0.5 in y, which is the gradient of the linear field -Ra*Pr*0.5*y and is
// therefore absorbed entirely into the pressure, leaving velocity and temperature unchanged.
// Ra = g*beta*dT*L^3/(nu*alpha) = Ra*Pr/Pr = Ra exactly; Pr = nu/alpha = Pr; the velocity scale
// alpha/L = 1, so the raw solver velocity already IS the benchmark's nondimensional velocity.
//
// THE CLAIM TESTED HERE: changing only T_ref must leave the velocity and temperature fields
// unchanged (to solver tolerance) while shifting pressure by a linear function of y. If that
// fails, the "equivalent up to a pressure constant" step of the derivation is wrong.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "DeVahlDavis1983.hpp"
#include "NaturalConvectionValidationUtils.hpp"
#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/thermal/ThermalProperties.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

using namespace cfd;
using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::MeshGeometry;
using cfd::physics::BoussinesqBuoyancy;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalStatus;

namespace {
constexpr Real kPr = cfd::validation::de_vahl_davis_1983::kPrandtlNumber;
constexpr Real kRa = 1.0e3;
constexpr Real kBeta = kRa * kPr;

SIMPLESettings flowSettings() {
  SIMPLESettings s;
  s.maxIterations = 10000;
  s.velocityRelaxation = 0.2;
  s.pressureRelaxation = 0.1;
  s.velocityTolerance = 1e-6;
  s.pressureTolerance = 1e-5;
  s.continuityTolerance = 1e-6;
  s.momentumSolver.maxIterations = 1000;
  s.momentumSolver.absoluteTolerance = 1e-9;
  s.momentumSolver.relativeTolerance = 1e-7;
  s.pressureSolver.maxIterations = 30000;
  s.pressureSolver.absoluteTolerance = 1e-7;
  s.pressureSolver.relativeTolerance = 1e-5;
  return s;
}

struct Fields {
  VectorField velocity{1, Vector2{0.0, 0.0}};
  ScalarField temperature{1, 0.0};
  ScalarField pressure{1, 0.0};
  bool ok{false};
};

Fields solve(Index n, Real tRef) {
  const auto mesh = MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  BoundaryConditionSet vel;
  for (const char* p : {"left", "right", "bottom", "top"})
    vel.set(mesh, p, std::make_unique<Wall>());
  BoundaryConditionSet pres;
  for (const auto& patch : mesh.boundaryPatches())
    pres.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  BoundaryConditionSet temp;
  temp.set(mesh, "left", std::make_unique<FixedTemperature>(1.0));
  temp.set(mesh, "right", std::make_unique<FixedTemperature>(0.0));
  temp.set(mesh, "top", std::make_unique<Adiabatic>());
  temp.set(mesh, "bottom", std::make_unique<Adiabatic>());

  const FluidProperties fluid(1.0, kPr);
  const ThermalProperties tp(1.0, 1.0);
  const ThermalSolver ts{};
  ScalarField temperature(mesh.numberOfCells(), 0.5);
  VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  ScalarField pressure(mesh.numberOfCells(), 0.0);
  SIMPLEResult flow;
  Fields out;
  for (Index outer = 0; outer < 400; ++outer) {
    const BoussinesqBuoyancy b(1.0, kBeta, tRef, Vector2{0.0, -1.0});
    const SIMPLE simple(flowSettings(), 0, nullptr, &temperature, &b);
    flow = simple.solve(mesh, fluid, vel, pres, velocity, pressure);
    if (flow.status != SIMPLEStatus::Converged) return out;
    velocity = flow.velocity;
    pressure = flow.pressure;
    const auto th = ts.solve(mesh, temperature, flow.massFlux, tp, temp);
    if (th.status != ThermalStatus::Converged) return out;
    Real maxChange = 0.0;
    for (Index i = 0; i < temperature.size(); ++i) {
      const Real next = temperature[i] + 0.3 * (th.temperature[i] - temperature[i]);
      maxChange = std::max(maxChange, std::abs(next - temperature[i]));
      temperature[i] = next;
    }
    if (maxChange < 1e-8) break;
  }
  out.velocity = velocity;
  out.temperature = temperature;
  out.pressure = pressure;
  out.ok = true;
  return out;
}

}  // namespace

int main() {
  const Index n = 10;
  std::printf("# UF-001.3 nondimensional-equivalence check, n = %lld, Ra = %.0f, Pr = %.2f\n",
              static_cast<long long>(n), kRa, kPr);
  std::printf("# Ra implied by the case = g*beta*dT*L^3/(nu*alpha) = 1*%.1f*1*1/(%.2f*1) = %.1f\n",
              kBeta, kPr, kBeta / kPr);
  std::printf("# Pr implied by the case = nu/alpha = %.2f/1 = %.2f\n", kPr, kPr);
  std::printf("# velocity scale alpha/L = 1 -> raw solver velocity IS the benchmark velocity\n\n");

  const auto a = solve(n, 0.5);   // the test's configuration
  const auto b = solve(n, 0.0);   // theta = T exactly, i.e. the benchmark's own source
  if (!a.ok || !b.ok) {
    std::printf("a solve failed (a %d b %d) -- inconclusive\n", a.ok, b.ok);
    return 1;
  }
  const auto mesh = MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  Real du = 0.0, dv = 0.0, dT = 0.0, uScale = 0.0;
  for (Index i = 0; i < a.velocity.size(); ++i) {
    du = std::max(du, std::abs(a.velocity[i].x - b.velocity[i].x));
    dv = std::max(dv, std::abs(a.velocity[i].y - b.velocity[i].y));
    dT = std::max(dT, std::abs(a.temperature[i] - b.temperature[i]));
    uScale = std::max(uScale, std::max(std::abs(a.velocity[i].x), std::abs(a.velocity[i].y)));
  }
  std::printf("T_ref = 0.5 (the test) vs T_ref = 0 (theta = T, the benchmark source):\n");
  std::printf("  max |du| %.3e   max |dv| %.3e   max |dT| %.3e   (velocity scale %.4f)\n", du, dv,
              dT, uScale);
  std::printf("  relative velocity difference %.3e\n", std::max(du, dv) / uScale);

  // The pressure difference must be a LINEAR function of y with slope -Ra*Pr*0.5.
  Real worstResidual = 0.0;
  const Real expectedSlope = -kBeta * 0.5;
  Real c0 = 0.0;
  {
    const Real dp = a.pressure[0] - b.pressure[0];
    c0 = dp - expectedSlope * mesh.cell(0).centroid().y;
  }
  for (Index i = 0; i < a.pressure.size(); ++i) {
    const Real dp = a.pressure[i] - b.pressure[i];
    const Real predicted = expectedSlope * mesh.cell(i).centroid().y + c0;
    worstResidual = std::max(worstResidual, std::abs(dp - predicted));
  }
  std::printf("  pressure difference vs the predicted linear field (slope %.1f):"
              " worst residual %.3e\n", expectedSlope, worstResidual);

  const bool pass = (std::max(du, dv) / uScale) < 1e-6 && dT < 1e-6;
  std::printf("\nVERDICT: velocity/temperature fields %s -> the case is%s equivalent to the\n"
              "benchmark problem up to the hydrostatic pressure shift.\n",
              pass ? "unchanged" : "CHANGED", pass ? "" : " NOT");
  return pass ? 0 : 1;
}

// P12-DIFF-002-UF-001.4/.5/.6: the differentially heated square cavity over a systematic grid
// family, with EVERY quantity recorded raw, and u_max / v_max evaluated on the SAME converged
// field by five independent extraction conventions.
//
// Investigation only: no production file and no authoritative test is modified. The case, the
// boundary conditions, the nondimensionalisation and every solver setting are copied verbatim
// from tests/integration/thermal/test_natural_convection_validation.cpp (frozen hash
// ca7ab2b2... in logs/00_freeze.log). The only deliberate deviation is `maxOuterIterations`,
// which is raised so that the outer Picard loop reaches its own 1e-8 tolerance on the finer
// grids instead of being truncated; the actual outer count and final change are reported for
// every grid so this is visible, and n = 10 is additionally run at the test's own 60 to prove
// the test's numbers are reproduced.
//
// de Vahl Davis (1983) definitions, established in UF-001.2 from the Wan/Patnaik/Wei (2001)
// tables (data/wan_2001.txt):
//   Table 2 "maximum vertical velocity (v) at the mid-height (y = 0.5)"  -> 3.679 at x = 0.179
//   Table 3 "maximum horizontal velocity (u) at the mid-width (x = 0.5)" -> 3.634 at y = 0.813
//   Table 5 / eq. (55)  Nu = integral_0^1 Nu_local dy                    -> 1.12
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
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

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::Adiabatic;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedTemperature;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::BoussinesqBuoyancy;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalResult;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalStatus;
using cfd::validation::ProfileSample;

namespace {

constexpr Real kLength = 1.0;
constexpr Real kHeight = 1.0;
constexpr Real kHot = 1.0;
constexpr Real kCold = 0.0;
constexpr Real kTRef = 0.5;
constexpr Real kGravity = 1.0;
constexpr Real kPrandtl = cfd::validation::de_vahl_davis_1983::kPrandtlNumber;
constexpr Real kRa = 1.0e3;
constexpr Real kBeta = kRa * kPrandtl;

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

// ---- the five extraction conventions, all applied to the SAME converged field -------------

// (a) what the test does today: the largest of the cell-centre samples (plus the wall zeros).
cfd::validation::Extremum discreteMax(const std::vector<ProfileSample>& p) {
  return cfd::validation::findMax(p);
}

// (b) sub-grid peak: fit a parabola through the peak sample and its two neighbours and take the
// vertex. For a smooth profile sampled at spacing h this estimates the true peak VALUE to
// O(h^4) and its LOCATION to O(h^2), instead of the O(h^2)-with-sawtooth-phase error of (a).
cfd::validation::Extremum parabolicMax(const std::vector<ProfileSample>& p) {
  std::size_t k = 0;
  for (std::size_t i = 0; i < p.size(); ++i) {
    if (p[i].value > p[k].value) k = i;
  }
  if (k == 0 || k + 1 >= p.size()) return {p[k].value, p[k].coordinate};
  const Real x0 = p[k - 1].coordinate, x1 = p[k].coordinate, x2 = p[k + 1].coordinate;
  const Real y0 = p[k - 1].value, y1 = p[k].value, y2 = p[k + 1].value;
  // Lagrange quadratic through the three points; vertex of the parabola.
  const Real d0 = (x0 - x1) * (x0 - x2);
  const Real d1 = (x1 - x0) * (x1 - x2);
  const Real d2 = (x2 - x0) * (x2 - x1);
  if (d0 == 0.0 || d1 == 0.0 || d2 == 0.0) return {y1, x1};
  // a x^2 + b x + c with a = sum y_i / d_i, b = -sum y_i (x_j + x_k) / d_i.
  const Real a = y0 / d0 + y1 / d1 + y2 / d2;
  const Real b = -(y0 * (x1 + x2) / d0 + y1 * (x0 + x2) / d1 + y2 * (x0 + x1) / d2);
  const Real c = y0 * x1 * x2 / d0 + y1 * x0 * x2 / d1 + y2 * x0 * x1 / d2;
  if (a >= 0.0) return {y1, x1};  // not a maximum; fall back to the sample
  const Real xv = -b / (2.0 * a);
  if (xv < x0 || xv > x2) return {y1, x1};
  return {a * xv * xv + b * xv + c, xv};
}

// (d) natural cubic spline through the samples, evaluated densely.
cfd::validation::Extremum splineMax(const std::vector<ProfileSample>& p) {
  const std::size_t n = p.size();
  if (n < 3) return cfd::validation::findMax(p);
  std::vector<Real> h(n - 1), alpha(n), l(n, 1.0), mu(n, 0.0), z(n, 0.0), cc(n, 0.0), bb(n, 0.0),
      dd(n, 0.0);
  for (std::size_t i = 0; i + 1 < n; ++i) h[i] = p[i + 1].coordinate - p[i].coordinate;
  for (std::size_t i = 1; i + 1 < n; ++i) {
    alpha[i] = 3.0 / h[i] * (p[i + 1].value - p[i].value) -
               3.0 / h[i - 1] * (p[i].value - p[i - 1].value);
  }
  for (std::size_t i = 1; i + 1 < n; ++i) {
    l[i] = 2.0 * (p[i + 1].coordinate - p[i - 1].coordinate) - h[i - 1] * mu[i - 1];
    mu[i] = h[i] / l[i];
    z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
  }
  for (std::size_t i = n - 1; i-- > 0;) {
    cc[i] = z[i] - mu[i] * cc[i + 1];
    bb[i] = (p[i + 1].value - p[i].value) / h[i] - h[i] * (cc[i + 1] + 2.0 * cc[i]) / 3.0;
    dd[i] = (cc[i + 1] - cc[i]) / (3.0 * h[i]);
  }
  cfd::validation::Extremum best{p[0].value, p[0].coordinate};
  for (std::size_t i = 0; i + 1 < n; ++i) {
    constexpr int kSteps = 400;
    for (int s = 0; s <= kSteps; ++s) {
      const Real t = h[i] * static_cast<Real>(s) / kSteps;
      const Real v = p[i].value + bb[i] * t + cc[i] * t * t + dd[i] * t * t * t;
      if (v > best.value) best = {v, p[i].coordinate + t};
    }
  }
  return best;
}

struct GridRow {
  Index n{0};
  bool flowConverged{false};
  bool thermalConverged{false};
  Index outerIterations{0};
  Real finalOuterChange{0.0};
  Index flowIterations{0};
  Real massImbalance{0.0};
  Real maxWallFlux{0.0};
  Real heatImbalance{0.0};
  Real qHot{0.0}, qCold{0.0};
  Real nuAvg{0.0};
  Real uDiscrete{0.0}, uDiscreteY{0.0};
  Real uParabolic{0.0}, uParabolicY{0.0};
  Real uSpline{0.0}, uSplineY{0.0};
  Real uGlobal{0.0};
  Real vDiscrete{0.0}, vDiscreteX{0.0};
  Real vParabolic{0.0}, vParabolicX{0.0};
  Real vSpline{0.0}, vSplineX{0.0};
  Real vGlobal{0.0};
  Real minTheta{0.0}, maxTheta{0.0};
  Real seconds{0.0};
};

GridRow run(Index n, Index maxOuter) {
  const Mesh mesh = MeshGeometry::createCartesian2D(n, n, kLength, kHeight);
  BoundaryConditionSet vel;
  vel.set(mesh, "left", std::make_unique<Wall>());
  vel.set(mesh, "right", std::make_unique<Wall>());
  vel.set(mesh, "bottom", std::make_unique<Wall>());
  vel.set(mesh, "top", std::make_unique<Wall>());
  BoundaryConditionSet pres;
  for (const auto& patch : mesh.boundaryPatches()) {
    pres.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  BoundaryConditionSet temp;
  temp.set(mesh, "left", std::make_unique<FixedTemperature>(kHot));
  temp.set(mesh, "right", std::make_unique<FixedTemperature>(kCold));
  temp.set(mesh, "top", std::make_unique<Adiabatic>());
  temp.set(mesh, "bottom", std::make_unique<Adiabatic>());

  const FluidProperties fluid(1.0, kPrandtl);
  const ThermalProperties thermalProps(1.0, 1.0);
  const ThermalSolver thermalSolver{};
  ScalarField temperature(mesh.numberOfCells(), kTRef);
  VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  ScalarField pressure(mesh.numberOfCells(), 0.0);
  SIMPLEResult flow;
  ThermalResult therm;
  constexpr Real kOuterRelax = 0.3;
  constexpr Real kOuterTol = 1e-8;
  Index outerUsed = 0;
  Real finalChange = 0.0;

  const auto t0 = std::chrono::steady_clock::now();
  for (Index outer = 0; outer < maxOuter; ++outer) {
    const BoussinesqBuoyancy buoyancy(fluid.density(), kBeta, kTRef,
                                      Vector2{0.0, -kGravity});
    const SIMPLE simple(flowSettings(), 0, nullptr, &temperature, &buoyancy);
    flow = simple.solve(mesh, fluid, vel, pres, velocity, pressure);
    outerUsed = outer + 1;
    if (flow.status != SIMPLEStatus::Converged) break;
    velocity = flow.velocity;
    pressure = flow.pressure;
    therm = thermalSolver.solve(mesh, temperature, flow.massFlux, thermalProps, temp);
    if (therm.status != ThermalStatus::Converged) break;
    ScalarField blended(temperature.size());
    Real maxChange = 0.0;
    for (Index i = 0; i < temperature.size(); ++i) {
      blended[i] = temperature[i] + kOuterRelax * (therm.temperature[i] - temperature[i]);
      maxChange = std::max(maxChange, std::abs(blended[i] - temperature[i]));
    }
    temperature = blended;
    finalChange = maxChange;
    if (maxChange < kOuterTol) break;
  }
  const auto t1 = std::chrono::steady_clock::now();

  GridRow r;
  r.n = n;
  r.flowConverged = (flow.status == SIMPLEStatus::Converged);
  r.thermalConverged = (therm.status == ThermalStatus::Converged);
  r.outerIterations = outerUsed;
  r.finalOuterChange = finalChange;
  r.flowIterations = flow.iterations;
  r.massImbalance = flow.globalMassImbalance;
  r.seconds = std::chrono::duration<double>(t1 - t0).count();
  if (!r.flowConverged || !r.thermalConverged) return r;

  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index f : patch.faceIds()) {
      r.maxWallFlux = std::max(r.maxWallFlux, std::abs(flow.massFlux[f]));
    }
  }
  r.qHot = cfd::validation::computeWallHeatFluxIntoFluid(mesh, n, n, temperature, 0, kHot, kLength,
                                                         kHeight);
  r.qCold = cfd::validation::computeWallHeatFluxIntoFluid(mesh, n, n, temperature, n - 1, kCold,
                                                          kLength, kHeight);
  r.heatImbalance =
      std::abs(r.qHot + r.qCold) / std::max(std::abs(r.qHot), std::abs(r.qCold));
  r.nuAvg = cfd::validation::computeAverageNusselt(
      cfd::validation::computeLocalNusseltAtHotWall(mesh, n, n, temperature, kHot, kLength));

  const auto uProfile =
      cfd::validation::extractUProfileAtMidWidth(mesh, n, n, flow.velocity, kLength / 2.0, kHeight);
  const auto vProfile = cfd::validation::extractVProfileAtMidHeight(mesh, n, n, flow.velocity,
                                                                    kHeight / 2.0, kLength);
  const auto ud = discreteMax(uProfile);
  const auto up = parabolicMax(uProfile);
  const auto us = splineMax(uProfile);
  const auto vd = discreteMax(vProfile);
  const auto vp = parabolicMax(vProfile);
  const auto vs = splineMax(vProfile);
  r.uDiscrete = ud.value;   r.uDiscreteY = ud.coordinate;
  r.uParabolic = up.value;  r.uParabolicY = up.coordinate;
  r.uSpline = us.value;     r.uSplineY = us.coordinate;
  r.vDiscrete = vd.value;   r.vDiscreteX = vd.coordinate;
  r.vParabolic = vp.value;  r.vParabolicX = vp.coordinate;
  r.vSpline = vs.value;     r.vSplineX = vs.coordinate;
  // (c) the GLOBAL field extremum -- a DIFFERENT convention from the literature's, measured
  // only to quantify how different it is.
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    r.uGlobal = std::max(r.uGlobal, flow.velocity[i].x);
    r.vGlobal = std::max(r.vGlobal, flow.velocity[i].y);
  }
  r.minTheta = temperature[0];
  r.maxTheta = temperature[0];
  for (Index i = 0; i < temperature.size(); ++i) {
    r.minTheta = std::min(r.minTheta, temperature[i]);
    r.maxTheta = std::max(r.maxTheta, temperature[i]);
  }
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  using cfd::validation::de_vahl_davis_1983::kRa1e3;
  std::vector<Index> grids = {10, 15, 20, 30, 40};
  Index maxOuter = 400;
  if (argc > 1) {
    grids.clear();
    for (int i = 1; i < argc; ++i) grids.push_back(std::atoll(argv[i]));
  }
  std::printf("# UF-001.4/.5 natural-convection grid family, Ra = %.0f, Pr = %.2f, beta = %.0f\n",
              kRa, kPrandtl, kBeta);
  std::printf("# LITERATURE (de Vahl Davis 1983, h->0 Richardson-extrapolated): "
              "Nu_avg %.3f, u_max %.3f (y %.3f), v_max %.3f (x %.3f)\n\n",
              kRa1e3.nuAvg, kRa1e3.uMax, kRa1e3.uMaxY, kRa1e3.vMax, kRa1e3.vMaxX);

  std::vector<GridRow> rows;
  for (const Index n : grids) {
    rows.push_back(run(n, maxOuter));
    const auto& r = rows.back();
    std::printf("## n = %lld  (%s)  outer %lld/%lld  final outer change %.3e  "
                "SIMPLE iters %lld  %.1f s\n",
                static_cast<long long>(r.n),
                (r.flowConverged && r.thermalConverged) ? "converged" : "NOT CONVERGED",
                static_cast<long long>(r.outerIterations), static_cast<long long>(maxOuter),
                r.finalOuterChange, static_cast<long long>(r.flowIterations), r.seconds);
    if (!r.flowConverged || !r.thermalConverged) continue;
    std::printf("   mass imbalance %.3e   max wall flux %.3e   heat imbalance %.3e"
                "   q_hot %.6f q_cold %.6f\n",
                r.massImbalance, r.maxWallFlux, r.heatImbalance, r.qHot, r.qCold);
    std::printf("   theta range [%.3e, %.9f]\n", r.minTheta, r.maxTheta);
    std::printf("   Nu_avg   %.6f   rel err vs literature %.4f\n", r.nuAvg,
                std::abs(r.nuAvg - kRa1e3.nuAvg) / kRa1e3.nuAvg);
    std::printf("   u_max  discrete %.6f (y %.4f) err %.4f | parabolic %.6f (y %.4f) err %.4f"
                " | spline %.6f (y %.4f) err %.4f | GLOBAL field %.6f\n",
                r.uDiscrete, r.uDiscreteY, std::abs(r.uDiscrete - kRa1e3.uMax) / kRa1e3.uMax,
                r.uParabolic, r.uParabolicY, std::abs(r.uParabolic - kRa1e3.uMax) / kRa1e3.uMax,
                r.uSpline, r.uSplineY, std::abs(r.uSpline - kRa1e3.uMax) / kRa1e3.uMax, r.uGlobal);
    std::printf("   v_max  discrete %.6f (x %.4f) err %.4f | parabolic %.6f (x %.4f) err %.4f"
                " | spline %.6f (x %.4f) err %.4f | GLOBAL field %.6f\n",
                r.vDiscrete, r.vDiscreteX, std::abs(r.vDiscrete - kRa1e3.vMax) / kRa1e3.vMax,
                r.vParabolic, r.vParabolicX, std::abs(r.vParabolic - kRa1e3.vMax) / kRa1e3.vMax,
                r.vSpline, r.vSplineX, std::abs(r.vSpline - kRa1e3.vMax) / kRa1e3.vMax, r.vGlobal);
  }

  // Machine-readable, for the order/extrapolation analysis in Python.
  std::printf("\n#CSV n,converged,nu_avg,u_discrete,u_discrete_y,u_parabolic,u_parabolic_y,"
              "u_spline,u_global,v_discrete,v_discrete_x,v_parabolic,v_parabolic_x,v_spline,"
              "v_global,mass_imb,heat_imb,outer,final_outer_change\n");
  for (const auto& r : rows) {
    std::printf("CSV,%lld,%d,%.10f,%.10f,%.6f,%.10f,%.6f,%.10f,%.10f,%.10f,%.6f,%.10f,%.6f,"
                "%.10f,%.10f,%.3e,%.3e,%lld,%.3e\n",
                static_cast<long long>(r.n), (r.flowConverged && r.thermalConverged) ? 1 : 0,
                r.nuAvg, r.uDiscrete, r.uDiscreteY, r.uParabolic, r.uParabolicY, r.uSpline,
                r.uGlobal, r.vDiscrete, r.vDiscreteX, r.vParabolic, r.vParabolicX, r.vSpline,
                r.vGlobal, r.massImbalance, r.heatImbalance,
                static_cast<long long>(r.outerIterations), r.finalOuterChange);
  }
  return 0;
}

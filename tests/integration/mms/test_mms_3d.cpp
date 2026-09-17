// P12-MESH-005 -- genuinely three-dimensional manufactured solutions for the
// production operators on the 3D Cartesian hexahedral mesh, and the
// refinement study of results/p12-mesh-005/acceptance_gate.md section F.
//
//   phi = sin(pi x) cos(pi y / 2) exp(z / 2) + x y z
//   u   = (1 + y/2, 1 + z/2, 1 + x/2)            (div u = 0)
//   psi = exp(0.3 x + 0.5 y + 0.7 z)             (monotone along every grid line)
//
// Every field varies in z and u has a z component that varies in x, so none
// of them is a 2D field extruded in z. Every derivative and forcing below is
// derived BY HAND from these closed forms; ForcingMatchesFiniteDifferences
// re-derives each one independently with 4th-order central differences of
// the closed forms (never with a discrete operator of the code under test).
//
// MMS3DTest.DISABLED_OperatorRefinementStudy is the acceptance-gate study
// (n = 8, 16, 32, 64; run explicitly in Release with
// --gtest_also_run_disabled_tests from the repository root; it writes
// results/p12-mesh-005/data/mms3d_operator_study.{json,md,txt}).
// MMS3DTest.OperatorErrorsDecreaseOnCoarseLevels is a light regular
// regression guard (n = 8, 16), not the gate.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ManufacturedFields.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Diffusion.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/thermal/ThermalSolver.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/GridConvergence.hpp"
#include "cfd/validation/ManufacturedSolutionStudy.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::discretization::ConvectionScheme;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::validation::CellMask;
using cfd::validation::ErrorNorms;
using cfd::validation::MMSLevel;
using cfd::validation::MMSStudy;
using cfd::validation::NormKind;

namespace {

const Real kPi = cfd::constants::pi;

// --- Closed forms and hand-derived derivatives --------------------------------
// phi = S C E + x y z with S = sin(pi x), C = cos(pi y / 2), E = exp(z / 2).
Real phi(const Vector3& p) {
  return (std::sin(kPi * p.x) * std::cos(0.5 * kPi * p.y) * std::exp(0.5 * p.z)) +
         (p.x * p.y * p.z);
}
// grad phi = (pi cos(pi x) C E + y z, -(pi/2) S sin(pi y/2) E + x z, (1/2) S C E + x y).
Vector3 gradPhi(const Vector3& p) {
  const Real s = std::sin(kPi * p.x);
  const Real c = std::cos(0.5 * kPi * p.y);
  const Real e = std::exp(0.5 * p.z);
  return Vector3{(kPi * std::cos(kPi * p.x) * c * e) + (p.y * p.z),
                 (-0.5 * kPi * s * std::sin(0.5 * kPi * p.y) * e) + (p.x * p.z),
                 (0.5 * s * c * e) + (p.x * p.y)};
}
// Laplacian: (-pi^2 - pi^2/4 + 1/4) S C E; the Laplacian of x y z is 0.
Real laplacianPhi(const Vector3& p) {
  return (0.25 - (1.25 * kPi * kPi)) * std::sin(kPi * p.x) * std::cos(0.5 * kPi * p.y) *
         std::exp(0.5 * p.z);
}
Vector3 velocity(const Vector3& p) {
  return Vector3{1.0 + (0.5 * p.y), 1.0 + (0.5 * p.z), 1.0 + (0.5 * p.x)};
}
Real psi(const Vector3& p) { return std::exp((0.3 * p.x) + (0.5 * p.y) + (0.7 * p.z)); }
// u . grad psi = psi (0.3 u + 0.5 v + 0.7 w) = psi (1.5 + 0.35 x + 0.15 y + 0.25 z).
Real advectionPsi(const Vector3& p) {
  return psi(p) * (1.5 + (0.35 * p.x) + (0.15 * p.y) + (0.25 * p.z));
}

constexpr Real kPoissonConductivity = 1.0;
constexpr Real kDensity = 1.0;
constexpr Real kSpecificHeat = 1.0;
constexpr Real kTransportConductivity = 0.1;

// -k lap(phi) = Q (ThermalSolver with zero mass flux).
Real poissonSource(const Vector3& p) { return -kPoissonConductivity * laplacianPhi(p); }
// rho cp u . grad(phi) - k lap(phi) = Q (EnergyEquation.hpp; div u = 0).
Real transportSource(const Vector3& p) {
  return (kDensity * kSpecificHeat * dot(velocity(p), gradPhi(p))) -
         (kTransportConductivity * laplacianPhi(p));
}

// --- Independent finite differences of the closed forms --------------------------
constexpr Real kStep = 1e-3;
Vector3 axis(int a) {
  return a == 0 ? Vector3{1.0, 0.0, 0.0}
                : (a == 1 ? Vector3{0.0, 1.0, 0.0} : Vector3{0.0, 0.0, 1.0});
}
Real firstDerivative(const std::function<Real(const Vector3&)>& f, const Vector3& p, int a) {
  const Vector3 d = axis(a) * kStep;
  return (f(p - (d * 2.0)) - (8.0 * f(p - d)) + (8.0 * f(p + d)) - f(p + (d * 2.0))) /
         (12.0 * kStep);
}
Real secondDerivative(const std::function<Real(const Vector3&)>& f, const Vector3& p, int a) {
  const Vector3 d = axis(a) * kStep;
  return (-f(p + (d * 2.0)) + (16.0 * f(p + d)) - (30.0 * f(p)) + (16.0 * f(p - d)) -
          f(p - (d * 2.0))) /
         (12.0 * kStep * kStep);
}
Vector3 fdGradient(const std::function<Real(const Vector3&)>& f, const Vector3& p) {
  return Vector3{firstDerivative(f, p, 0), firstDerivative(f, p, 1), firstDerivative(f, p, 2)};
}
Real fdLaplacian(const std::function<Real(const Vector3&)>& f, const Vector3& p) {
  return secondDerivative(f, p, 0) + secondDerivative(f, p, 1) + secondDerivative(f, p, 2);
}

// |a - b| relative to max(1, |b|): relative for the O(1)-O(10) values here,
// without dividing by a value that happens to be near zero.
Real relativeError(Real a, Real b) { return std::abs(a - b) / std::max(1.0, std::abs(b)); }

// --- One refinement level ---------------------------------------------------------------
ScalarField sample(const Mesh& mesh, const std::function<Real(const Vector3&)>& f) {
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = f(cell.centroid());
  return field;
}
VectorField sampleVector(const Mesh& mesh, const std::function<Vector3(const Vector3&)>& f) {
  VectorField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) field[cell.id()] = f(cell.centroid());
  return field;
}
SurfaceField massFlux(const Mesh& mesh) {
  SurfaceField flux(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    flux[face.id()] = kDensity * dot(velocity(face.centroid()), face.areaVector());
  }
  return flux;
}
bool allFinite(const ScalarField& f) {
  for (Index i = 0; i < f.size(); ++i) {
    if (!std::isfinite(f[i])) return false;
  }
  return true;
}

cfd::thermal::ThermalSolverSettings gateSolverSettings() {
  cfd::thermal::ThermalSolverSettings settings;  // acceptance_gate.md section F
  settings.linearSolver.relativeTolerance = 1e-10;
  settings.linearSolver.absoluteTolerance = 1e-14;
  settings.linearSolver.maxIterations = 20000;
  settings.tolerance = 1e-10;
  settings.maxIterations = 200;
  return settings;
}

const char* thermalStatusName(cfd::thermal::ThermalStatus status) {
  switch (status) {
    case cfd::thermal::ThermalStatus::Converged:
      return "Converged";
    case cfd::thermal::ThermalStatus::MaxIterations:
      return "MaxIterations";
    case cfd::thermal::ThermalStatus::LinearSolveFailure:
      return "LinearSolveFailure";
    case cfd::thermal::ThermalStatus::NonFiniteState:
      return "NonFiniteState";
    case cfd::thermal::ThermalStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "unknown";
}

MMSLevel runLevel(Index n) {
  const auto start = std::chrono::steady_clock::now();
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(
      cfd::mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0));
  MMSLevel level;
  level.name = std::to_string(n) + "x" + std::to_string(n) + "x" + std::to_string(n);
  level.nx = n;
  level.ny = n;
  level.cells = mesh.numberOfCells();
  level.h = cfd::validation::representativeGridSize(1.0, mesh.numberOfCells(), 3);

  const CellMask ring = cfd::validation::boundaryAdjacentCells(mesh, 1);
  const CellMask interior = cfd::validation::invertMask(ring);
  const CellMask band2 = cfd::validation::boundaryAdjacentCells(mesh, 2);
  const CellMask interior2 = cfd::validation::invertMask(band2);

  const auto phiBcs = cfd::test::makeExactBoundaries(mesh, phi);
  const auto psiBcs = cfd::test::makeExactBoundaries(mesh, psi);
  const ScalarField phiCells = sample(mesh, phi);
  const ScalarField psiCells = sample(mesh, psi);
  const VectorField gradExact = sampleVector(mesh, gradPhi);

  // F1: Green-Gauss gradient.
  const VectorField gg = cfd::discretization::gradient(mesh, phiCells, phiBcs);
  level.errors.emplace_back(
      "F1_gg_gradient", cfd::validation::computeVectorErrorNorms(mesh, gg, gradExact).magnitude);
  level.errors.emplace_back(
      "F1_gg_gradient_boundary_ring",
      cfd::validation::computeVectorErrorNorms(mesh, gg, gradExact, &ring).magnitude);
  // F2 / F3: least-squares gradient.
  const VectorField lsq =
      cfd::discretization::gradient(mesh, phiCells, phiBcs, GradientScheme::LeastSquares);
  level.errors.emplace_back(
      "F2_lsq_gradient_interior",
      cfd::validation::computeVectorErrorNorms(mesh, lsq, gradExact, &interior).magnitude);
  level.errors.emplace_back(
      "F3_lsq_gradient", cfd::validation::computeVectorErrorNorms(mesh, lsq, gradExact).magnitude);
  // F4: explicit diffusion operator (Gamma = 1) against the exact Laplacian.
  const ScalarField lap = cfd::discretization::diffusion(mesh, phiCells, 1.0, phiBcs);
  level.errors.emplace_back(
      "F4_diffusion", cfd::validation::computeErrorNorms(mesh, lap, sample(mesh, laplacianPhi)));

  // F5: Poisson solve through the production implicit assembly + linear solver.
  const auto settings = gateSolverSettings();
  const auto poisson = cfd::thermal::ThermalSolver(settings).solve(
      mesh, ScalarField(mesh.numberOfCells(), 0.0), SurfaceField(mesh.numberOfFaces(), 0.0),
      cfd::thermal::ThermalProperties(kPoissonConductivity, kSpecificHeat), phiBcs,
      sample(mesh, poissonSource));
  const bool poissonOk = poisson.converged() && allFinite(poisson.temperature);
  if (poissonOk) {
    level.errors.emplace_back(
        "F5_poisson", cfd::validation::computeErrorNorms(mesh, poisson.temperature, phiCells));
  }
  level.diagnostics.emplace_back("F5_outer_iterations", static_cast<Real>(poisson.iterations));
  level.diagnostics.emplace_back("F5_last_linear_iterations",
                                 static_cast<Real>(poisson.linearIterations));

  // F6 / F7: explicit convection of psi with the exact divergence-free mass flux.
  const SurfaceField flux = massFlux(mesh);
  const ScalarField convExact =
      sample(mesh, [](const Vector3& p) { return kDensity * advectionPsi(p); });
  const ScalarField upwind =
      cfd::discretization::convection(mesh, psiCells, flux, psiBcs, ConvectionScheme::Upwind);
  level.errors.emplace_back("F6_convection_upwind",
                            cfd::validation::computeErrorNorms(mesh, upwind, convExact));
  for (const auto scheme :
       {ConvectionScheme::Central, ConvectionScheme::LinearUpwind, ConvectionScheme::QUICK}) {
    const std::string name(cfd::discretization::convectionSchemeName(scheme));
    const ScalarField conv = cfd::discretization::convection(mesh, psiCells, flux, psiBcs, scheme);
    level.errors.emplace_back(
        "F7_convection_" + name + "_interior",
        cfd::validation::computeErrorNorms(mesh, conv, convExact, &interior2));
    level.errors.emplace_back("F7_convection_" + name + "_global",
                              cfd::validation::computeErrorNorms(mesh, conv, convExact));
  }

  // F8: convection-diffusion solve (production energy convection = upwind).
  const auto transport = cfd::thermal::ThermalSolver(settings).solve(
      mesh, ScalarField(mesh.numberOfCells(), 0.0), flux,
      cfd::thermal::ThermalProperties(kTransportConductivity, kSpecificHeat), phiBcs,
      sample(mesh, transportSource));
  const bool transportOk = transport.converged() && allFinite(transport.temperature);
  if (transportOk) {
    level.errors.emplace_back(
        "F8_convection_diffusion",
        cfd::validation::computeErrorNorms(mesh, transport.temperature, phiCells));
  }
  level.diagnostics.emplace_back("F8_outer_iterations", static_cast<Real>(transport.iterations));
  level.diagnostics.emplace_back("F8_last_linear_iterations",
                                 static_cast<Real>(transport.linearIterations));

  level.accepted = poissonOk && transportOk;
  level.solverStatus = std::string("poisson ") + thermalStatusName(poisson.status) +
                       ", transport " + thermalStatusName(transport.status);
  if (!level.accepted) level.rejectionReason = level.solverStatus;
  level.runtimeSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  return level;
}

Real normOf(const ErrorNorms& e, NormKind kind) {
  return kind == NormKind::L1 ? e.l1 : (kind == NormKind::L2 ? e.l2 : e.linf);
}
const char* normName(NormKind kind) {
  return kind == NormKind::L1 ? "L1" : (kind == NormKind::L2 ? "L2" : "Linf");
}

// One pre-registered gate item (acceptance_gate.md section F).
struct GatedOrder {
  std::string quantity;
  NormKind norm;
  Real expected;
};

std::vector<GatedOrder> gatedOrders() {
  std::vector<GatedOrder> g;
  const auto all = [&](const std::string& q, Real p) {
    for (const auto kind : {NormKind::L1, NormKind::L2, NormKind::Linf}) g.push_back({q, kind, p});
  };
  all("F1_gg_gradient", 2.0);
  all("F2_lsq_gradient_interior", 2.0);
  g.push_back({"F3_lsq_gradient", NormKind::L1, 2.0});
  g.push_back({"F3_lsq_gradient", NormKind::L2, 1.5});
  g.push_back({"F3_lsq_gradient", NormKind::Linf, 1.0});
  all("F4_diffusion", 2.0);
  all("F5_poisson", 2.0);
  all("F6_convection_upwind", 1.0);
  all("F7_convection_central_interior", 2.0);
  all("F7_convection_linear_upwind_interior", 2.0);
  all("F7_convection_quick_interior", 2.0);
  all("F8_convection_diffusion", 1.0);
  return g;
}

// p = ln(Ec / Ef) / ln(hc / hf) for every consecutive pair.
std::vector<Real> pairwiseOrders(const MMSStudy& study, const std::string& quantity,
                                 NormKind kind) {
  std::vector<Real> p;
  for (std::size_t k = 0; k + 1 < study.levels.size(); ++k) {
    const Real ec = normOf(study.error(k, quantity), kind);
    const Real ef = normOf(study.error(k + 1, quantity), kind);
    p.push_back(std::log(ec / ef) / std::log(study.levels[k].h / study.levels[k + 1].h));
  }
  return p;
}

std::string fmt(const char* format, Real value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

MMSStudy runStudy(const std::vector<Index>& sizes) {
  MMSStudy study;
  study.name = "p12_mesh_005_operators_3d";
  study.description =
      "P12-MESH-005: production operators on the 3D Cartesian hexahedral mesh, unit cube, "
      "per-face exact Dirichlet data";
  study.manufacturedSolution =
      "phi = sin(pi x) cos(pi y/2) exp(z/2) + x y z; u = (1 + y/2, 1 + z/2, 1 + x/2); "
      "psi = exp(0.3x + 0.5y + 0.7z)";
  study.coefficients = {{"poisson_conductivity", kPoissonConductivity},
                        {"density", kDensity},
                        {"specific_heat", kSpecificHeat},
                        {"transport_conductivity", kTransportConductivity}};
  study.configuration = {
      {"mesh", "MeshGeometry::createCartesian3D, n x n x n"},
      {"h", "representativeGridSize(1, n^3, 3) = 1/n"},
      {"order", "p = ln(Ec/Ef) / ln(hc/hf), consecutive pairs; gate on the finest pair"},
      {"F1", "gradient(GreenGauss) vs grad(phi), global"},
      {"F2/F3", "gradient(LeastSquares) vs grad(phi), interior (not boundary-adjacent) / global"},
      {"F4", "diffusion(phi, 1) vs lap(phi), global"},
      {"F5", "ThermalSolver, zero mass flux, k = 1, per-face Dirichlet: -k lap(phi) = Q"},
      {"F6/F7",
       "convection(psi, rho u.Sf) vs rho u.grad(psi); F7 interior = >2 layers from boundary"},
      {"F8",
       "ThermalSolver, upwind energy convection, k = 0.1: rho cp u.grad(phi) - k lap(phi) = Q"},
      {"linear solver", "BiCGSTAB rel 1e-10, abs 1e-14, <= 20000; outer tolerance 1e-10"}};
  for (const Index n : sizes) {
    study.levels.push_back(runLevel(n));
    const auto& level = study.levels.back();
    std::printf("level %s: %s (%.1f s)\n", level.name.c_str(), level.solverStatus.c_str(),
                level.runtimeSeconds);
    std::fflush(stdout);
  }
  return study;
}

}  // namespace

// F0: the manufactured solution and every forcing, checked against 4th-order
// central differences of the closed forms at 20 fixed pseudo-random points.
TEST(MMS3DTest, ForcingMatchesFiniteDifferencesOfClosedForms) {
  unsigned long long state = 20260915ULL;
  const auto next = [&]() {
    state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return 0.05 + (0.9 * static_cast<Real>(state >> 11) / 9007199254740992.0);
  };
  const auto u0 = [](const Vector3& p) { return velocity(p).x; };
  const auto u1 = [](const Vector3& p) { return velocity(p).y; };
  const auto u2 = [](const Vector3& p) { return velocity(p).z; };
  Real worst = 0.0;
  for (int k = 0; k < 20; ++k) {
    const Vector3 p{next(), next(), next()};
    const Vector3 g = fdGradient(phi, p);
    const Vector3 exact = gradPhi(p);
    worst = std::max({worst, relativeError(g.x, exact.x), relativeError(g.y, exact.y),
                      relativeError(g.z, exact.z)});
    const Real lap = fdLaplacian(phi, p);
    worst = std::max(worst, relativeError(lap, laplacianPhi(p)));
    const Real divergence =
        firstDerivative(u0, p, 0) + firstDerivative(u1, p, 1) + firstDerivative(u2, p, 2);
    worst = std::max(worst, std::abs(divergence));
    worst = std::max(worst, relativeError(dot(velocity(p), fdGradient(psi, p)), advectionPsi(p)));
    worst = std::max(worst, relativeError(-kPoissonConductivity * lap, poissonSource(p)));
    worst = std::max(worst, relativeError((kDensity * kSpecificHeat * dot(velocity(p), g)) -
                                              (kTransportConductivity * lap),
                                          transportSource(p)));
    // Genuinely 3D: every field changes with z.
    EXPECT_GT(std::abs(exact.z), 0.0);
    EXPECT_GT(std::abs(firstDerivative(psi, p, 2)), 0.0);
  }
  std::printf("F0: worst relative deviation from 4th-order central differences: %.3e\n", worst);
  EXPECT_LE(worst, 1e-7);
}

// Light regression guard (NOT the gate): every gated error decreases from 8^3 to 16^3.
TEST(MMS3DTest, OperatorErrorsDecreaseOnCoarseLevels) {
  const MMSStudy study = runStudy({8, 16});
  for (const auto& level : study.levels) ASSERT_TRUE(level.accepted) << level.rejectionReason;
  for (const auto& item : gatedOrders()) {
    const Real coarse = normOf(study.error(0, item.quantity), item.norm);
    const Real fine = normOf(study.error(1, item.quantity), item.norm);
    EXPECT_LT(fine, coarse) << item.quantity << " " << normName(item.norm);
  }
}

// The acceptance-gate refinement study (acceptance_gate.md section F).
TEST(MMS3DTest, DISABLED_OperatorRefinementStudy) {
  MMSStudy study = runStudy({8, 16, 32, 64});
  for (const auto& level : study.levels) {
    ASSERT_TRUE(level.accepted) << level.name << ": " << level.rejectionReason;
  }

  std::string table = "P12-MESH-005 3D operator refinement study (unit cube, n = 8, 16, 32, 64)\n";
  table += "p = ln(Ec/Ef)/ln(hc/hf); gate: finest-pair p >= 0.75 * expected and E decreasing\n\n";
  // Every measured quantity (gated or reported), every norm, every pair.
  std::vector<std::string> quantities;
  for (const auto& [name, unused] : study.levels.front().errors) quantities.push_back(name);
  for (const auto& quantity : quantities) {
    for (const auto kind : {NormKind::L1, NormKind::L2, NormKind::Linf}) {
      std::string row = quantity +
                        std::string(quantity.size() < 40 ? 40 - quantity.size() : 1, ' ') +
                        normName(kind) + (kind == NormKind::Linf ? "  " : "    ");
      for (std::size_t k = 0; k < study.levels.size(); ++k) {
        row += fmt(" %.4e", normOf(study.error(k, quantity), kind));
      }
      row += "  p:";
      for (const Real p : pairwiseOrders(study, quantity, kind)) row += fmt(" %.3f", p);
      table += row + "\n";
    }
  }
  table += "\nh:";
  for (const auto& level : study.levels) table += fmt(" %.6f", level.h);
  table += "\n\nGATE\n";

  bool allPassed = true;
  for (const auto& item : gatedOrders()) {
    const auto p = pairwiseOrders(study, item.quantity, item.norm);
    const Real threshold = 0.75 * item.expected;
    bool decreasing = true;
    for (std::size_t k = 0; k + 1 < study.levels.size(); ++k) {
      decreasing = decreasing && normOf(study.error(k + 1, item.quantity), item.norm) <
                                     normOf(study.error(k, item.quantity), item.norm);
    }
    const bool passed = decreasing && p.back() >= threshold;
    allPassed = allPassed && passed;
    std::string detail = "pairwise p";
    for (const Real v : p) detail += fmt(" %.3f", v);
    detail += "; finest " + fmt("%.3f", p.back()) + " vs threshold " + fmt("%.3f", threshold) +
              " (expected " + fmt("%.1f", item.expected) + "); " +
              (decreasing ? "decreasing" : "NOT decreasing");
    study.gates.push_back({item.quantity + " " + normName(item.norm), passed, detail});
    table += std::string(passed ? "PASS " : "FAIL ") + item.quantity + " " + normName(item.norm) +
             ": " + detail + "\n";
    EXPECT_TRUE(passed) << item.quantity << " " << normName(item.norm) << ": " << detail;
  }
  table += allPassed ? "\nDECISION: every gated item PASSES\n" : "\nDECISION: GATE FAILED\n";
  table += "\nper-level solver diagnostics:\n";
  for (const auto& level : study.levels) {
    table += "  " + level.name + ": " + level.solverStatus;
    for (const auto& [name, value] : level.diagnostics)
      table += ", " + name + " " + fmt("%.0f", value);
    table += fmt(", %.1f s\n", level.runtimeSeconds);
  }
  std::printf("\n%s", table.c_str());
  std::fflush(stdout);

  const std::filesystem::path dir = "results/p12-mesh-005/data";
  std::filesystem::create_directories(dir);
  cfd::validation::writeMMSReport((dir / "mms3d_operator_study.json").string(), study);
  std::ofstream(dir / "mms3d_operator_study.md") << cfd::validation::mmsReportMarkdown(study);
  std::ofstream(dir / "mms3d_operator_study.txt") << table;
}

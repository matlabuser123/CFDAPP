// P12-MESH-006 gate G4 -- a genuinely three-dimensional manufactured solution for the production
// SIMPLE (u, v, w, p coupled; Rhie-Chow face flux, automatic for 3D), run from rest.
//
// U = curl(0, -F, G) / pi with G = sin(pi x) sin(pi y) e^{z/2}, F = sin(pi x) sin(pi z) e^{-y/2}:
//   u =  sin(pi x) (cos(pi y) e^{z/2} + cos(pi z) e^{-y/2})
//   v = -cos(pi x) sin(pi y) e^{z/2}
//   w = -cos(pi x) sin(pi z) e^{-y/2}
// div U = 0 exactly, U . n = 0 on all six faces of the unit cube, and every component depends on x,
// y and z (nothing is a 2D field extruded in z). Every term is an eigenfunction of the Laplacian:
// lap U = (1/4 - 2 pi^2) U.
//   p = cos(pi x) cos(pi y) cos(pi z) + x y z / 2
// Forcing f = rho (U . grad) U + grad p - mu lap U, rho = 1, mu = 0.1 (as the 2D SIMPLE MMS).
// Every derivative below is derived by hand; ForcingMatchesFiniteDifferences re-derives each with
// 4th-order central differences of the closed forms (never a discrete operator of the code).
//
// Setup = the 2D SIMPLE MMS (test_mms_simple.cpp): exact Dirichlet velocity per boundary face,
// exact FixedGradient(grad p . n) per face, reference cell 0, relaxation 0.8/0.4, outer tolerance
// 1e-8 (absolute, every residual), BiCGSTAB inner rel 1e-3 (pressure: Jacobi), central convection,
// Green-Gauss. MMSSimple3DTest.DISABLED_RefinementStudy is the gate study (n = 8, 16, 32; run
// explicitly in Release from the repository root; reports to results/p12-mesh-006/data/).
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ManufacturedFields.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/GridConvergence.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"
#include "cfd/validation/ManufacturedSolutionStudy.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::validation::ErrorNorms;
using cfd::validation::MMSLevel;
using cfd::validation::MMSStudy;
using cfd::validation::NormKind;

namespace {

const Real kPi = cfd::constants::pi;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;

struct Derivatives {
  Vector3 value;  // (u, v, w)
  Vector3 gradU;  // (u_x, u_y, u_z)
  Vector3 gradV;
  Vector3 gradW;
  Vector3 laplacian;
};

// Hand-derived (see the header): with S = sin, C = cos of pi x, pi y, pi z; Ez = e^{z/2}, Ey =
// e^{-y/2}.
Derivatives exact(const Vector3& p) {
  const Real sx = std::sin(kPi * p.x);
  const Real cx = std::cos(kPi * p.x);
  const Real sy = std::sin(kPi * p.y);
  const Real cy = std::cos(kPi * p.y);
  const Real sz = std::sin(kPi * p.z);
  const Real cz = std::cos(kPi * p.z);
  const Real ez = std::exp(0.5 * p.z);
  const Real ey = std::exp(-0.5 * p.y);
  Derivatives d;
  d.value = Vector3{sx * ((cy * ez) + (cz * ey)), -cx * sy * ez, -cx * sz * ey};
  d.gradU = Vector3{kPi * cx * ((cy * ez) + (cz * ey)), sx * ((-kPi * sy * ez) - (0.5 * cz * ey)),
                    sx * ((0.5 * cy * ez) - (kPi * sz * ey))};
  d.gradV = Vector3{kPi * sx * sy * ez, -kPi * cx * cy * ez, -0.5 * cx * sy * ez};
  d.gradW = Vector3{kPi * sx * sz * ey, 0.5 * cx * sz * ey, -kPi * cx * cz * ey};
  const Real eigen = 0.25 - (2.0 * kPi * kPi);
  d.laplacian = d.value * eigen;
  return d;
}

Vector3 velocity(const Vector3& p) { return exact(p).value; }

Real pressure(const Vector3& p) {
  return (std::cos(kPi * p.x) * std::cos(kPi * p.y) * std::cos(kPi * p.z)) +
         (0.5 * p.x * p.y * p.z);
}

Vector3 pressureGradient(const Vector3& p) {
  const Real sx = std::sin(kPi * p.x);
  const Real cx = std::cos(kPi * p.x);
  const Real sy = std::sin(kPi * p.y);
  const Real cy = std::cos(kPi * p.y);
  const Real sz = std::sin(kPi * p.z);
  const Real cz = std::cos(kPi * p.z);
  return Vector3{(-kPi * sx * cy * cz) + (0.5 * p.y * p.z),
                 (-kPi * cx * sy * cz) + (0.5 * p.x * p.z),
                 (-kPi * cx * cy * sz) + (0.5 * p.x * p.y)};
}

// f = rho (U . grad) U + grad p - mu lap U.
Vector3 forcing(const Vector3& p) {
  const Derivatives d = exact(p);
  const Vector3 convective{dot(d.value, d.gradU), dot(d.value, d.gradV), dot(d.value, d.gradW)};
  return (convective * kDensity) + pressureGradient(p) - (d.laplacian * kViscosity);
}

// --- Independent 4th-order central differences of the closed forms --------------------------
constexpr Real kStep = 1e-3;
Vector3 axis(int a) {
  return a == 0 ? Vector3{1.0, 0.0, 0.0}
                : (a == 1 ? Vector3{0.0, 1.0, 0.0} : Vector3{0.0, 0.0, 1.0});
}
Real fd1(const std::function<Real(const Vector3&)>& f, const Vector3& p, int a) {
  const Vector3 h = axis(a) * kStep;
  return (f(p - (h * 2.0)) - (8.0 * f(p - h)) + (8.0 * f(p + h)) - f(p + (h * 2.0))) /
         (12.0 * kStep);
}
Real fd2(const std::function<Real(const Vector3&)>& f, const Vector3& p, int a) {
  const Vector3 h = axis(a) * kStep;
  return (-f(p + (h * 2.0)) + (16.0 * f(p + h)) - (30.0 * f(p)) + (16.0 * f(p - h)) -
          f(p - (h * 2.0))) /
         (12.0 * kStep * kStep);
}
Vector3 fdGradient(const std::function<Real(const Vector3&)>& f, const Vector3& p) {
  return Vector3{fd1(f, p, 0), fd1(f, p, 1), fd1(f, p, 2)};
}
Real fdLaplacian(const std::function<Real(const Vector3&)>& f, const Vector3& p) {
  return fd2(f, p, 0) + fd2(f, p, 1) + fd2(f, p, 2);
}
Real relativeError(Real a, Real b) { return std::abs(a - b) / std::max(1.0, std::abs(b)); }

// Face average of U . n over an axis-aligned h x h face (3 x 3 Gauss-Legendre, O(h^6)), with n
// the face's own area-vector direction.
Real exactNormalVelocity(const cfd::mesh::Face& face, Real h) {
  const Vector3 normal = face.areaVector() * (1.0 / face.area());
  const int a = std::abs(normal.x) > 0.5 ? 0 : (std::abs(normal.y) > 0.5 ? 1 : 2);
  const Vector3 s = axis((a + 1) % 3) * (0.5 * h);
  const Vector3 t = axis((a + 2) % 3) * (0.5 * h);
  const Real node = std::sqrt(0.6);
  const Real nodes[3] = {-node, 0.0, node};
  const Real weights[3] = {5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0};
  Real sum = 0.0;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      const Vector3 x = face.centroid() + (s * nodes[i]) + (t * nodes[j]);
      sum += weights[i] * weights[j] * dot(velocity(x), normal);
    }
  }
  return 0.25 * sum;
}

// --- One refinement level --------------------------------------------------------------------
MMSLevel runLevel(Index n) {
  const auto start = std::chrono::steady_clock::now();
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(
      cfd::mesh::MeshGeometry::createCartesian3D(n, n, n, 1.0, 1.0, 1.0));
  BoundaryConditionSet vb;
  BoundaryConditionSet pb;
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto& face = mesh.face(patch.faceIds().front());
    vb.set(mesh, patch.name(), std::make_unique<cfd::boundary::Inlet>(velocity(face.centroid())));
    const Vector3 normal = face.areaVector() * (1.0 / face.area());
    pb.set(mesh, patch.name(),
           std::make_unique<cfd::boundary::FixedGradient>(
               dot(pressureGradient(face.centroid()), normal)));
  }
  const Index cells = mesh.numberOfCells();
  VectorField source(cells);
  VectorField exactVelocity(cells);
  ScalarField exactPressure(cells);
  for (const auto& c : mesh.cells()) {
    source[c.id()] = forcing(c.centroid());
    exactVelocity[c.id()] = velocity(c.centroid());
    exactPressure[c.id()] = pressure(c.centroid());
  }
  // The 2D SIMPLE MMS settings (MMSCases.cpp simpleSettings), unchanged.
  cfd::pressure_velocity::SIMPLESettings settings;
  settings.maxIterations = 40000;
  settings.velocityRelaxation = 0.8;
  settings.pressureRelaxation = 0.4;
  settings.velocityTolerance = 1e-8;
  settings.pressureTolerance = 1e-8;
  settings.continuityTolerance = 1e-8;
  settings.convectionScheme = cfd::discretization::ConvectionScheme::Central;
  settings.momentumSolver.absoluteTolerance = 1e-12;
  settings.momentumSolver.relativeTolerance = 1e-3;
  settings.momentumSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-12;
  settings.pressureSolver.relativeTolerance = 1e-3;
  settings.pressureSolver.maxIterations = 5000;
  settings.pressureSolver.preconditioner = cfd::algebra::PreconditionerType::Jacobi;
  settings.robustness.linearSolverFallback.enabled = true;
  cfd::pressure_velocity::SIMPLE simple(settings, 0);
  simple.setMomentumSource(&source);
  const auto r = simple.solve(mesh, cfd::physics::FluidProperties(kDensity, kViscosity), vb, pb,
                              VectorField(cells), ScalarField(cells, 0.0));

  MMSLevel level;
  level.name = std::to_string(n) + "x" + std::to_string(n) + "x" + std::to_string(n);
  level.nx = n;
  level.ny = n;
  level.cells = cells;
  level.h = cfd::validation::representativeGridSize(1.0, cells, 3);
  level.iterations = r.iterations;
  level.massImbalance = r.globalMassImbalance;
  level.solverStatus = std::string(cfd::validation::simpleStatusName(r.status));
  bool finite = true;
  for (Index i = 0; i < cells; ++i)
    finite = finite && cfd::isFinite(r.velocity[i]) && std::isfinite(r.pressure[i]);
  level.accepted =
      r.converged() && finite && r.faceFlux == cfd::pressure_velocity::FaceFluxScheme::RhieChow;
  if (!level.accepted) {
    level.rejectionReason = level.solverStatus + (finite ? "" : ", non-finite");
    level.runtimeSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return level;
  }
  for (const auto component : {0, 1, 2}) {
    ScalarField numeric(cells);
    ScalarField expected(cells);
    for (Index i = 0; i < cells; ++i) {
      numeric[i] =
          component == 0 ? r.velocity[i].x : (component == 1 ? r.velocity[i].y : r.velocity[i].z);
      expected[i] = component == 0 ? exactVelocity[i].x
                                   : (component == 1 ? exactVelocity[i].y : exactVelocity[i].z);
    }
    level.errors.emplace_back(component == 0 ? "u" : (component == 1 ? "v" : "w"),
                              cfd::validation::computeErrorNorms(mesh, numeric, expected));
  }
  level.errors.emplace_back(
      "velocity",
      cfd::validation::computeVectorErrorNorms(mesh, r.velocity, exactVelocity).magnitude);
  level.errors.emplace_back(
      "p", cfd::validation::computeGaugeInvariantErrorNorms(mesh, r.pressure, exactPressure));
  const auto continuity = cfd::physics::evaluateContinuity(mesh, r.massFlux);
  ScalarField divergence(cells);
  for (const auto& c : mesh.cells())
    divergence[c.id()] = continuity.cellImbalance[c.id()] / (kDensity * c.volume());
  level.errors.emplace_back("continuity", cfd::validation::computeErrorNorms(mesh, divergence));
  // Face flux against the exact face-integrated flux, as a normal velocity (reported).
  SurfaceField numericPerArea(mesh.numberOfFaces());
  SurfaceField exactPerArea(mesh.numberOfFaces());
  for (const auto& face : mesh.faces()) {
    numericPerArea[face.id()] = r.massFlux[face.id()] / (kDensity * face.area());
    exactPerArea[face.id()] = exactNormalVelocity(face, 1.0 / static_cast<Real>(n));
  }
  level.errors.emplace_back(
      "face_flux", cfd::validation::computeFaceErrorNorms(mesh, numericPerArea, exactPerArea));
  level.diagnostics = {{"global_mass_imbalance", r.globalMassImbalance},
                       {"final_u_residual", r.finalUResidual},
                       {"final_v_residual", r.finalVResidual},
                       {"final_w_residual", r.finalWResidual},
                       {"final_pressure_residual", r.finalPressureResidual},
                       {"final_continuity_residual", r.finalContinuityResidual}};
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
std::string fmt(const char* format, Real value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

MMSStudy runStudy(const std::vector<Index>& sizes) {
  MMSStudy study;
  study.name = "p12_mesh_006_simple_3d";
  study.description =
      "P12-MESH-006: production SIMPLE (u, v, w, p; Rhie-Chow) on the 3D Cartesian hexahedral "
      "mesh, "
      "closed unit cube, from rest";
  study.manufacturedSolution =
      "U = curl(0, -F, G)/pi, G = sin(pi x) sin(pi y) e^{z/2}, F = sin(pi x) sin(pi z) e^{-y/2}; "
      "p = cos(pi x) cos(pi y) cos(pi z) + xyz/2 (compared modulo gauge)";
  study.coefficients = {{"density", kDensity}, {"viscosity", kViscosity}};
  study.configuration = {
      {"solver",
       "cfd::pressure_velocity::SIMPLE + setMomentumSource, face flux automatic (rhie_chow)"},
      {"convection", "central"},
      {"gradient", "green_gauss"},
      {"relaxation", "velocity 0.8, pressure 0.4"},
      {"outer_tolerance", "1e-8 (u, v, w, pressure, continuity; absolute)"},
      {"inner_solvers", "BiCGSTAB rel 1e-3 / abs 1e-12 (pressure: Jacobi)"},
      {"velocity_bc", "exact Dirichlet (Inlet) on every boundary face"},
      {"pressure_bc", "exact FixedGradient(grad p . n) per face; reference cell 0"},
      {"initial_condition", "U = 0, p = 0"},
      {"h", "representativeGridSize(1, n^3, 3) = 1/n"}};
  for (const Index n : sizes) {
    study.levels.push_back(runLevel(n));
    const auto& level = study.levels.back();
    std::printf("level %s: %s, %llu iterations (%.1f s)\n", level.name.c_str(),
                level.solverStatus.c_str(), static_cast<unsigned long long>(level.iterations),
                level.runtimeSeconds);
    std::fflush(stdout);
  }
  return study;
}

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

bool decreasing(const MMSStudy& study, const std::string& quantity, NormKind kind) {
  for (std::size_t k = 0; k + 1 < study.levels.size(); ++k) {
    if (!(normOf(study.error(k + 1, quantity), kind) < normOf(study.error(k, quantity), kind))) {
      return false;
    }
  }
  return true;
}

}  // namespace

// G4.0: the manufactured solution and the forcing against 4th-order central differences.
TEST(MMSSimple3DTest, ForcingMatchesFiniteDifferencesOfClosedForms) {
  unsigned long long state = 20260915ULL;
  const auto next = [&]() {
    state = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
    return 0.05 + (0.9 * static_cast<Real>(state >> 11) / 9007199254740992.0);
  };
  const std::function<Real(const Vector3&)> u = [](const Vector3& p) { return velocity(p).x; };
  const std::function<Real(const Vector3&)> v = [](const Vector3& p) { return velocity(p).y; };
  const std::function<Real(const Vector3&)> w = [](const Vector3& p) { return velocity(p).z; };
  const std::function<Real(const Vector3&)> pr = [](const Vector3& p) { return pressure(p); };
  Real worst = 0.0;
  Real worstDivergence = 0.0;
  for (int k = 0; k < 20; ++k) {
    const Vector3 x{next(), next(), next()};
    const Derivatives d = exact(x);
    const Vector3 gu = fdGradient(u, x);
    const Vector3 gv = fdGradient(v, x);
    const Vector3 gw = fdGradient(w, x);
    const Vector3 gp = fdGradient(pr, x);
    for (const auto& [a, b] : {std::pair<Vector3, Vector3>{gu, d.gradU},
                               {gv, d.gradV},
                               {gw, d.gradW},
                               {gp, pressureGradient(x)}}) {
      worst = std::max(
          {worst, relativeError(a.x, b.x), relativeError(a.y, b.y), relativeError(a.z, b.z)});
    }
    const Vector3 lap{fdLaplacian(u, x), fdLaplacian(v, x), fdLaplacian(w, x)};
    worst = std::max({worst, relativeError(lap.x, d.laplacian.x),
                      relativeError(lap.y, d.laplacian.y), relativeError(lap.z, d.laplacian.z)});
    worstDivergence = std::max(worstDivergence, std::abs(gu.x + gv.y + gw.z));
    const Vector3 value = velocity(x);
    const Vector3 f =
        Vector3{dot(value, gu), dot(value, gv), dot(value, gw)} * kDensity + gp - lap * kViscosity;
    const Vector3 fe = forcing(x);
    worst = std::max(
        {worst, relativeError(f.x, fe.x), relativeError(f.y, fe.y), relativeError(f.z, fe.z)});
    // Genuinely 3D: every velocity component changes with z and with x.
    EXPECT_GT(std::abs(gu.z) + std::abs(gv.z) + std::abs(gw.z), 0.0);
    EXPECT_GT(std::abs(gv.x), 0.0);
    EXPECT_GT(std::abs(gw.x), 0.0);
  }
  // Zero normal velocity on every face of the cube.
  for (const Real s : {0.13, 0.5, 0.91}) {
    for (const Real t : {0.27, 0.77}) {
      EXPECT_NEAR(velocity(Vector3{0.0, s, t}).x, 0.0, 1e-15);
      EXPECT_NEAR(velocity(Vector3{1.0, s, t}).x, 0.0, 1e-15);
      EXPECT_NEAR(velocity(Vector3{s, 0.0, t}).y, 0.0, 1e-15);
      EXPECT_NEAR(velocity(Vector3{s, 1.0, t}).y, 0.0, 1e-15);
      EXPECT_NEAR(velocity(Vector3{s, t, 0.0}).z, 0.0, 1e-15);
      EXPECT_NEAR(velocity(Vector3{s, t, 1.0}).z, 0.0, 1e-15);
    }
  }
  std::printf(
      "G4.0: worst relative deviation from 4th-order central differences %.3e, max |div U| %.3e\n",
      worst, worstDivergence);
  EXPECT_LE(worst, 1e-7);
  EXPECT_LE(worstDivergence, 1e-7);
}

// Light regular guard (NOT the gate): converges from rest on 6^3 and 12^3 and every error
// decreases.
TEST(MMSSimple3DTest, ErrorsDecreaseOnCoarseLevels) {
  const MMSStudy study = runStudy({6, 12});
  for (const auto& level : study.levels) ASSERT_TRUE(level.accepted) << level.rejectionReason;
  for (const std::string q : {"u", "v", "w", "p"}) {
    for (const auto kind : {NormKind::L1, NormKind::L2}) {
      EXPECT_LT(normOf(study.error(1, q), kind), normOf(study.error(0, q), kind))
          << q << " " << normName(kind);
    }
  }
}

// The acceptance-gate study (acceptance_gate.md G4).
TEST(MMSSimple3DTest, DISABLED_RefinementStudy) {
  MMSStudy study = runStudy({8, 16, 32});
  for (const auto& level : study.levels)
    ASSERT_TRUE(level.accepted) << level.name << ": " << level.rejectionReason;

  std::string table = "P12-MESH-006 3D SIMPLE MMS (unit cube, closed, from rest; n = 8, 16, 32)\n";
  table += "p = ln(Ec/Ef)/ln(hc/hf); gate on the finest pair (16 -> 32)\n\n";
  for (const std::string q : {"u", "v", "w", "velocity", "p", "face_flux", "continuity"}) {
    for (const auto kind : {NormKind::L1, NormKind::L2, NormKind::Linf}) {
      std::string row = q + std::string(12 - q.size(), ' ') + normName(kind) +
                        (kind == NormKind::Linf ? "  " : "    ");
      for (std::size_t k = 0; k < study.levels.size(); ++k)
        row += fmt(" %.4e", normOf(study.error(k, q), kind));
      if (q != "continuity") {
        row += "  p:";
        for (const Real p : pairwiseOrders(study, q, kind)) row += fmt(" %.3f", p);
      }
      table += row + "\n";
    }
  }
  table += "\nGATE\n";
  bool allPassed = true;
  const auto gate = [&](const std::string& label, bool passed, const std::string& detail) {
    study.gates.push_back({label, passed, detail});
    table += std::string(passed ? "PASS " : "FAIL ") + label + ": " + detail + "\n";
    allPassed = allPassed && passed;
    EXPECT_TRUE(passed) << label << ": " << detail;
  };
  // G4.1 velocity: u, v, w L1 and L2 finest-pair order in [1.6, 2.4], every norm decreasing.
  for (const std::string q : {"u", "v", "w"}) {
    for (const auto kind : {NormKind::L1, NormKind::L2, NormKind::Linf}) {
      gate("G4.1 " + q + " " + normName(kind) + " decreasing", decreasing(study, q, kind),
           "8 -> 16 -> 32");
    }
    for (const auto kind : {NormKind::L1, NormKind::L2}) {
      const Real p = pairwiseOrders(study, q, kind).back();
      gate("G4.1 " + q + " " + normName(kind) + " order", p >= 1.6 && p <= 2.4,
           fmt("finest-pair p %.3f in [1.6, 2.4]", p));
    }
  }
  // G4.2 pressure (modulo gauge): L1, L2 decreasing and finest-pair order in [1.5, 2.4]; Linf
  // decreasing.
  for (const auto kind : {NormKind::L1, NormKind::L2, NormKind::Linf}) {
    gate(std::string("G4.2 p ") + normName(kind) + " decreasing", decreasing(study, "p", kind),
         "8 -> 16 -> 32");
  }
  for (const auto kind : {NormKind::L1, NormKind::L2}) {
    const Real p = pairwiseOrders(study, "p", kind).back();
    gate(std::string("G4.2 p ") + normName(kind) + " order", p >= 1.5 && p <= 2.4,
         fmt("finest-pair p %.3f in [1.5, 2.4]", p));
  }
  // G4.3 continuity and global mass; G4.4 every level Converged from rest, finite, Rhie-Chow.
  for (std::size_t k = 0; k < study.levels.size(); ++k) {
    gate("G4.4 solve at " + study.levels[k].name, study.levels[k].accepted,
         study.levels[k].solverStatus + ", finite, rhie_chow, " +
             std::to_string(study.levels[k].iterations) + " iterations from rest");
    const Real linf = study.error(k, "continuity").linf;
    gate("G4.3 continuity Linf per volume <= 1e-5 at " + study.levels[k].name, linf <= 1e-5,
         fmt("%.3e", linf));
    const Real mass = study.levels[k].massImbalance.value_or(1.0);
    gate("G4.3 global mass imbalance <= 1e-14 at " + study.levels[k].name, mass <= 1e-14,
         fmt("%.3e", mass));
  }
  table += allPassed ? "\nDECISION: every gated item PASSES\n" : "\nDECISION: GATE FAILED\n";
  table += "\nper-level solver diagnostics:\n";
  for (const auto& level : study.levels) {
    table += "  " + level.name + ": " + level.solverStatus + ", iterations " +
             std::to_string(level.iterations);
    for (const auto& [name, value] : level.diagnostics)
      table += ", " + name + " " + fmt("%.3e", value);
    table += fmt(", %.1f s\n", level.runtimeSeconds);
  }
  std::printf("\n%s", table.c_str());
  std::fflush(stdout);
  const std::filesystem::path dir = "results/p12-mesh-006/data";
  std::filesystem::create_directories(dir);
  cfd::validation::writeMMSReport((dir / "mms_simple3d.json").string(), study);
  std::ofstream(dir / "mms_simple3d.md") << cfd::validation::mmsReportMarkdown(study);
  std::ofstream(dir / "mms_simple3d.txt") << table;
}

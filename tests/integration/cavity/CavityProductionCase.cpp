#include "CavityProductionCase.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include "GhiaRe100.hpp"
#include "GhiaRe1000.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/validation/ErrorNorms.hpp"

namespace cfd::validation::cavity {

namespace {

using Table = std::array<ghia_re100::Sample, 17>;

const Table& ghiaU(Real reynolds) {
  if (reynolds == 100.0) return ghia_re100::kCenterlineU;
  if (reynolds == 1000.0) return ghia_re1000::kCenterlineU;
  throw std::invalid_argument("cavity: no Ghia table for this Reynolds number");
}

const Table& ghiaV(Real reynolds) {
  if (reynolds == 100.0) return ghia_re100::kCenterlineV;
  if (reynolds == 1000.0) return ghia_re1000::kCenterlineV;
  throw std::invalid_argument("cavity: no Ghia table for this Reynolds number");
}

// Tabulated value of `table` at `coordinate` (the station must exist).
Real tabulated(const Table& table, Real coordinate) {
  for (const auto& sample : table) {
    if (std::abs(sample.coordinate - coordinate) < 1e-12) return sample.value;
  }
  throw std::invalid_argument("cavity: not a Ghia station");
}

ErrorNorms stationErrors(const std::vector<ProfileSample>& profile, const Table& table) {
  std::vector<Real> numeric;
  std::vector<Real> reference;
  for (const auto& sample : table) {
    numeric.push_back(interpolateProfile(profile, sample.coordinate));
    reference.push_back(sample.value);
  }
  return computeSampleErrorNorms(numeric, reference);
}

std::string format(const char* pattern, Real a, Real b = 0.0) {
  char buffer[160];
  std::snprintf(buffer, sizeof(buffer), pattern, a, b);
  return buffer;
}

const ErrorNorms* findError(const ValidationRun& run, const std::string& name) {
  for (const auto& [key, norms] : run.level.errors) {
    if (key == name) return &norms;
  }
  return nullptr;
}

std::optional<Real> findDiagnostic(const ValidationRun& run, const std::string& name) {
  for (const auto& [key, value] : run.level.diagnostics) {
    if (key == name) return value;
  }
  return std::nullopt;
}

}  // namespace

pressure_velocity::SIMPLESettings cavitySettings(const CavityRunSpec& spec) {
  pressure_velocity::SIMPLESettings settings;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = kOuterTolerance;
  settings.pressureTolerance = kOuterTolerance;
  settings.continuityTolerance = kOuterTolerance;
  settings.convectionScheme = spec.scheme;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  if (spec.n <= 20) {
    settings.maxIterations = 6000;
    settings.pressureSolver.maxIterations = 2000;
    settings.pressureSolver.absoluteTolerance = 1e-10;
    settings.pressureSolver.relativeTolerance = 1e-8;
  } else if (spec.n <= 40) {
    settings.maxIterations = 8000;
    settings.pressureSolver.maxIterations = 2000;
    settings.pressureSolver.absoluteTolerance = 1e-8;
    settings.pressureSolver.relativeTolerance = 1e-6;
  } else {
    // 80x80's documented settings, reused unchanged for 160x160; only the
    // outer iteration cap grows with the grid (a cap, not a tolerance).
    settings.maxIterations = spec.n <= 80 ? 20000 : 40000;
    settings.pressureSolver.maxIterations = 5000;
    settings.pressureSolver.absoluteTolerance = 1e-7;
    settings.pressureSolver.relativeTolerance = 1e-5;
  }
  settings.robustness.linearSolverFallback.enabled = true;
  return settings;
}

std::string caseName(Real reynolds) {
  return "cavity_re" + std::to_string(static_cast<long long>(std::lround(reynolds)));
}

std::vector<Station> convergenceStations(Real reynolds) {
  const Table& u = ghiaU(reynolds);
  const Table& v = ghiaV(reynolds);
  if (reynolds == 100.0) {
    return {{"u_center", "u(0.5, 0.5)", tabulated(u, 0.5)},
            {"v_center", "v(0.5, 0.5)", tabulated(v, 0.5)},
            {"u_y0.4531", "u(0.5, 0.4531), Ghia station near u_min", tabulated(u, 0.4531)},
            {"v_x0.2344", "v(0.2344, 0.5), Ghia station near v_max", tabulated(v, 0.2344)},
            {"v_x0.8047", "v(0.8047, 0.5), Ghia station near v_min", tabulated(v, 0.8047)}};
  }
  return {{"u_center", "u(0.5, 0.5)", tabulated(u, 0.5)},
          {"v_center", "v(0.5, 0.5)", tabulated(v, 0.5)},
          {"u_y0.1719", "u(0.5, 0.1719), Ghia station near u_min", tabulated(u, 0.1719)},
          {"v_x0.1563", "v(0.1563, 0.5), Ghia station near v_max", tabulated(v, 0.1563)},
          {"v_x0.9063", "v(0.9063, 0.5), Ghia station near v_min", tabulated(v, 0.9063)}};
}

ValidationRun runCavity(const CavityRunSpec& spec, const std::string& csvDirectory) {
  using boundary::BoundaryConditionSet;
  const Index n = spec.n;
  const mesh::Mesh mesh = mesh::MeshGeometry::createCartesian2D(n, n, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<boundary::Wall>());
  velocityBoundaries.set(mesh, "right", std::make_unique<boundary::Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<boundary::Wall>());
  velocityBoundaries.set(mesh, "top",
                         std::make_unique<boundary::MovingWall>(Vector2{kLidVelocity, 0.0}));
  BoundaryConditionSet pressureBoundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    pressureBoundaries.set(mesh, patch.name(), std::make_unique<boundary::FixedGradient>(0.0));
  }
  // Re = rho U_lid L / mu with L = 1.
  const physics::FluidProperties fluid(kDensity, kDensity * kLidVelocity / spec.reynolds);
  const pressure_velocity::SIMPLE simple(cavitySettings(spec), /*referenceCell=*/0);

  const auto start = std::chrono::steady_clock::now();
  const pressure_velocity::SIMPLEResult result =
      simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                   fields::VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}),
                   fields::ScalarField(mesh.numberOfCells(), 0.0));
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  const std::string scheme(discretization::convectionSchemeName(spec.scheme));
  ValidationRun run = makeSimpleValidationRun(caseName(spec.reynolds), spec.reynolds, scheme,
                                              GridSpec{"", n, n, 1.0, 1.0}, result,
                                              kMassImbalanceTolerance, seconds);
  run.checks.push_back(
      {"solve_accepted", run.level.accepted,
       run.level.solverStatus + (run.level.accepted
                                     ? " after " + std::to_string(result.iterations) + " iterations"
                                     : ": " + run.level.rejectionReason)});
  if (!run.level.accepted) return run;  // nothing is measured on a rejected solve

  Real maxWallFlux = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      maxWallFlux = std::max(maxWallFlux, std::abs(result.massFlux[faceId]));
    }
  }
  Real maxSpeed = 0.0;
  for (Index i = 0; i < result.velocity.size(); ++i) {
    maxSpeed = std::max(maxSpeed, std::hypot(result.velocity[i].x, result.velocity[i].y));
  }

  const auto uProfile = extractVerticalProfileU(mesh, n, n, result.velocity, 0.5, kLidVelocity);
  const auto vProfile = extractHorizontalProfileV(mesh, n, n, result.velocity, 0.5);
  run.level.errors.emplace_back("u_centerline", stationErrors(uProfile, ghiaU(spec.reynolds)));
  run.level.errors.emplace_back("v_centerline", stationErrors(vProfile, ghiaV(spec.reynolds)));
  if (!csvDirectory.empty()) {
    std::filesystem::create_directories(csvDirectory);
    (void)computeGhiaErrors(uProfile, ghiaU(spec.reynolds), csvDirectory + "/centerline_u.csv", "y",
                            "u");
    (void)computeGhiaErrors(vProfile, ghiaV(spec.reynolds), csvDirectory + "/centerline_v.csv", "x",
                            "v");
  }

  run.level.diagnostics.emplace_back("max_wall_normal_flux", maxWallFlux);
  run.level.diagnostics.emplace_back("max_velocity_magnitude", maxSpeed);
  for (const auto& station : convergenceStations(spec.reynolds)) {
    Real value = 0.0;
    if (station.name == "u_center") {
      value = interpolateProfile(uProfile, 0.5);
    } else if (station.name == "v_center") {
      value = interpolateProfile(vProfile, 0.5);
    } else if (station.name.rfind("u_y", 0) == 0) {
      value = interpolateProfile(uProfile, std::stod(station.name.substr(3)));
    } else {
      value = interpolateProfile(vProfile, std::stod(station.name.substr(3)));
    }
    run.level.diagnostics.emplace_back(station.name, value);
  }

  run.checks.push_back(
      {"wall_normal_flux", maxWallFlux <= 1e-6,
       format("max |mass flux| through the walls %.3g (bound 1e-6)", maxWallFlux)});
  return run;
}

void addGhiaErrorCheck(ValidationRun& run, Real maxUL2, Real maxVL2) {
  const ErrorNorms* u = findError(run, "u_centerline");
  const ErrorNorms* v = findError(run, "v_centerline");
  if (u == nullptr || v == nullptr) {
    run.checks.push_back({"ghia_error", false, "no centerline errors (solve rejected)"});
    return;
  }
  const bool passed = u->l2 <= maxUL2 && v->l2 <= maxVL2;
  run.checks.push_back({"ghia_error", passed,
                        format("u L2 %.4e", u->l2) + format(" (bound %.3g),", maxUL2) +
                            format(" v L2 %.4e", v->l2) + format(" (bound %.3g)", maxVL2)});
}

void addBoundednessCheck(ValidationRun& run) {
  const auto speed = findDiagnostic(run, "max_velocity_magnitude");
  if (!speed.has_value()) {
    run.checks.push_back({"bounded", false, "no velocity field (solve rejected)"});
    return;
  }
  run.checks.push_back({"bounded", *speed <= kLidVelocity * (1.0 + 1e-9),
                        format("max |U| %.6f vs lid speed %.3g", *speed, kLidVelocity)});
}

GridConvergenceStudy cavityGridConvergence(const std::string& name, const std::string& description,
                                           const std::array<const ValidationRun*, 3>& coarseToFine,
                                           Real reynolds, Real formalOrder) {
  std::vector<std::string> names;
  std::vector<QuantitySpec> quantities;
  for (const auto& station : convergenceStations(reynolds)) {
    names.push_back(station.name);
    QuantitySpec q;
    q.name = station.name;
    q.description = station.description;
    q.reference = station.ghia;
    q.referenceKind = "benchmark";
    q.options.formalOrder = formalOrder;
    // Outer tolerance 1e-6 -> sampled values reproducible to ~1e-6 (the
    // P12-NUM-005 cavity study's setting; measured: tightening the outer
    // tolerance to 1e-8 on 80x80 moves the Ghia L2 errors by < 1e-5).
    q.options.absoluteNoise = 1e-5;
    q.options.gridIndependenceThreshold = 0.01;
    quantities.push_back(q);
  }
  std::vector<GridStudyEntry> entries;
  for (const ValidationRun* run : coarseToFine) {
    entries.push_back(toGridStudyEntry(*run, 1.0, 1.0, names));
  }
  return analyzeGridStudy(name, description, std::move(entries), quantities);
}

}  // namespace cfd::validation::cavity

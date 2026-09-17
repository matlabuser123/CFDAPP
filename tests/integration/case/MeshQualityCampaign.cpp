#include "MeshQualityCampaign.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/thermal/EnergyEquation.hpp"
#include "cfd/thermal/ThermalSolver.hpp"

namespace cfd::test::meshq {

namespace {

constexpr Real kPi = constants::pi;

// S(t) = sin^2(pi t) and its derivatives.
struct S {
  Real v, d1, d2, d3;
};
S sFactor(Real t) {
  return S{std::sin(kPi * t) * std::sin(kPi * t), kPi * std::sin(2.0 * kPi * t),
           2.0 * kPi * kPi * std::cos(2.0 * kPi * t),
           -4.0 * kPi * kPi * kPi * std::sin(2.0 * kPi * t)};
}

// splitmix64 -> [-1, 1): deterministic, integer arithmetic only.
Real hashUnit(std::uint64_t i, std::uint64_t j, std::uint64_t component) {
  std::uint64_t z = (i * 0x9E3779B97F4A7C15ULL) ^ (j * 0xC2B2AE3D27D4EB4FULL) ^
                    (component * 0x165667B19E3779F9ULL);
  z += 0x9E3779B97F4A7C15ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z = z ^ (z >> 31);
  return (static_cast<Real>(z >> 11) * 0x1.0p-53 * 2.0) - 1.0;
}

cfd::io::MeshConfig quadMesh(Index n, const std::vector<Vector2>& vertices) {
  cfd::io::MeshConfig mesh;
  mesh.type = "structured_quad";
  mesh.nx = n;
  mesh.ny = n;
  mesh.vertices = vertices;
  return mesh;
}

double secondsSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::string simpleStatusName(cfd::pressure_velocity::SIMPLEStatus status) {
  using cfd::pressure_velocity::SIMPLEStatus;
  switch (status) {
    case SIMPLEStatus::Converged:
      return "Converged";
    case SIMPLEStatus::MaxIterations:
      return "MaxIterations";
    case SIMPLEStatus::MomentumFailure:
      return "MomentumFailure";
    case SIMPLEStatus::PressureCorrectionFailure:
      return "PressureCorrectionFailure";
    case SIMPLEStatus::NonFiniteState:
      return "NonFiniteState";
    case SIMPLEStatus::InvalidConfiguration:
      return "InvalidConfiguration";
    case SIMPLEStatus::Cancelled:
      return "Cancelled";
    case SIMPLEStatus::Stagnated:
      return "Stagnated";
    case SIMPLEStatus::Diverging:
      return "Diverging";
  }
  return "Unknown";
}

}  // namespace

Vector2 exactVelocity(const Vector2& p) {
  const S x = sFactor(p.x);
  const S y = sFactor(p.y);
  return Vector2{x.v * y.d1 / kPi, -x.d1 * y.v / kPi};
}

Real exactPressure(const Vector2& p) { return std::cos(kPi * p.x) * std::cos(2.0 * kPi * p.y); }

Real exactTemperature(const Vector2& p) { return std::sin(kPi * p.x) * std::sin(kPi * p.y); }

Real exactMeanTemperature() { return 4.0 / (kPi * kPi); }

Vector2 momentumForcing(const Vector2& p) {
  const S x = sFactor(p.x);
  const S y = sFactor(p.y);
  const Real u = x.v * y.d1 / kPi;
  const Real v = -x.d1 * y.v / kPi;
  const Real ux = x.d1 * y.d1 / kPi;
  const Real uy = x.v * y.d2 / kPi;
  const Real uxx = x.d2 * y.d1 / kPi;
  const Real uyy = x.v * y.d3 / kPi;
  const Real vx = -x.d2 * y.v / kPi;
  const Real vy = -x.d1 * y.d1 / kPi;
  const Real vxx = -x.d3 * y.v / kPi;
  const Real vyy = -x.d1 * y.d2 / kPi;
  const Real px = -kPi * std::sin(kPi * p.x) * std::cos(2.0 * kPi * p.y);
  const Real py = -2.0 * kPi * std::cos(kPi * p.x) * std::sin(2.0 * kPi * p.y);
  return Vector2{(kDensity * ((u * ux) + (v * uy))) + px - (kViscosity * (uxx + uyy)),
                 (kDensity * ((u * vx) + (v * vy))) + py - (kViscosity * (vxx + vyy))};
}

Real heatSource(const Vector2& p) {
  const Vector2 u = exactVelocity(p);
  const Real tx = kPi * std::cos(kPi * p.x) * std::sin(kPi * p.y);
  const Real ty = kPi * std::sin(kPi * p.x) * std::cos(kPi * p.y);
  const Real laplacian = -2.0 * kPi * kPi * exactTemperature(p);
  return (kDensity * kSpecificHeat * ((u.x * tx) + (u.y * ty))) - (kConductivity * laplacian);
}

cfd::io::MeshConfig cartesianMesh(Index nx, Index ny) {
  cfd::io::MeshConfig mesh;
  mesh.type = "structured_cartesian";
  mesh.nx = nx;
  mesh.ny = ny;
  return mesh;
}

cfd::io::MeshConfig gradedMesh(Index n, Real ratio) {
  cfd::io::MeshConfig mesh = cartesianMesh(n, n);
  if (ratio != 1.0) {
    cfd::io::MeshGradingConfig grading;
    grading.y = cfd::mesh::AxisGrading{cfd::mesh::GradingType::Geometric, ratio,
                                       cfd::mesh::GradingCluster::Both};
    mesh.grading = grading;
  }
  return mesh;
}

cfd::io::MeshConfig smoothDistortedMesh(Index n, Real amplitude) {
  std::vector<Vector2> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real X = static_cast<Real>(i) / static_cast<Real>(n);
      const Real Y = static_cast<Real>(j) / static_cast<Real>(n);
      Real x = X + (amplitude * std::sin(kPi * X) * std::sin(2.0 * kPi * Y));
      Real y = Y + (amplitude * std::sin(2.0 * kPi * X) * std::sin(kPi * Y));
      if (i == 0 || i == n || j == 0 || j == n) {  // boundary vertices exactly on the edges
        x = X;
        y = Y;
      }
      vertices.push_back(Vector2{x, y});
    }
  }
  return quadMesh(n, vertices);
}

cfd::io::MeshConfig roughDistortedMesh(Index n, Real fraction) {
  const Real h = 1.0 / static_cast<Real>(n);
  std::vector<Vector2> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real X = static_cast<Real>(i) * h;
      const Real Y = static_cast<Real>(j) * h;
      const bool boundary = i == 0 || i == n || j == 0 || j == n;
      vertices.push_back(boundary ? Vector2{X, Y}
                                  : Vector2{X + (fraction * h * hashUnit(i, j, 0)),
                                            Y + (fraction * h * hashUnit(i, j, 1))});
    }
  }
  return quadMesh(n, vertices);
}

cfd::io::CaseDefinition caseDefinition(const cfd::io::MeshConfig& mesh,
                                       const Discretization& discretization) {
  cfd::io::CaseDefinition d = cfd::io::CaseReader{}.read("cases/heated_cavity");
  d.caseConfig.name = "P12-MESH-004 manufactured-solution cavity";
  d.caseConfig.description =
      "Unit-square cavity with walls; manufactured solution (MeshQualityCampaign.hpp) driven by "
      "an injected source. Written by the P12-MESH-004 campaign.";
  d.geometry.type = "rectangle";
  d.geometry.length = 1.0;
  d.geometry.height = 1.0;
  d.mesh = mesh;
  d.physics.density = kDensity;
  d.physics.dynamicViscosity = kViscosity;
  d.physics.thermal = cfd::io::ThermalPhysicsConfig{kConductivity, kSpecificHeat, 0.0};
  d.boundaries.patches.clear();
  for (const char* name : {"left", "right", "bottom", "top"}) {
    cfd::io::PatchBoundaryConfig patch;
    patch.velocity.type = "wall";
    patch.pressure.type = "fixed_gradient";
    patch.pressure.value = 0.0;
    patch.temperature = cfd::io::TemperatureBoundarySpec{"fixed_temperature", 0.0};
    d.boundaries.patches.emplace(name, patch);
  }
  d.solver.type = "SIMPLE";
  d.solver.maxIterations = 20000;
  d.solver.velocityRelaxation = discretization.velocityRelaxation;
  d.solver.pressureRelaxation = discretization.pressureRelaxation;
  d.solver.velocityTolerance = discretization.tolerance;
  d.solver.pressureTolerance = discretization.tolerance;
  d.solver.continuityTolerance = discretization.tolerance;
  d.solver.momentumSolver = cfd::io::LinearSolverSpec{"BiCGSTAB", "CPU", 1e-10, 1e-8, 500};
  d.solver.pressureSolver = cfd::io::LinearSolverSpec{"CG", "CPU", 1e-10, 1e-8, 5000};
  d.solver.convectionScheme = discretization.convectionScheme;
  d.solver.gradientScheme = discretization.gradientScheme;
  d.solver.nonOrthogonalCorrections = discretization.nonOrthogonalCorrections;
  d.initialConditions.velocity = Vector2{0.0, 0.0};
  d.initialConditions.pressure = 0.0;
  return d;
}

Run run(const std::filesystem::path& directory, const cfd::io::CaseDefinition& definition) {
  Run r;
  const auto start = std::chrono::steady_clock::now();
  std::optional<cfd::io::SimulationSetup> setup;
  try {
    cfd::io::CaseWriter::write(directory, definition);
    const cfd::io::CaseDefinition reread = cfd::io::CaseReader{}.read(directory);
    setup.emplace(cfd::io::CaseBuilder{}.build(reread));
  } catch (const cfd::Error& e) {
    r.buildError = e.what();
    r.seconds = secondsSince(start);
    return r;
  }
  r.built = true;
  const cfd::mesh::Mesh& mesh = setup->mesh;
  r.quality = setup->meshQuality;
  r.cells = mesh.numberOfCells();
  Real area = 0.0;
  for (const auto& cell : mesh.cells()) area += cell.volume();
  r.h = std::sqrt(area / static_cast<Real>(r.cells));

  cfd::fields::VectorField force(mesh.numberOfCells());
  cfd::fields::ScalarField heat(mesh.numberOfCells());
  cfd::fields::VectorField exactU(mesh.numberOfCells());
  cfd::fields::ScalarField exactP(mesh.numberOfCells());
  cfd::fields::ScalarField exactT(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    force[cell.id()] = momentumForcing(cell.centroid());
    heat[cell.id()] = heatSource(cell.centroid());
    exactU[cell.id()] = exactVelocity(cell.centroid());
    exactP[cell.id()] = exactPressure(cell.centroid());
    exactT[cell.id()] = exactTemperature(cell.centroid());
  }

  cfd::pressure_velocity::SIMPLE simple(setup->solverSettings, /*referenceCell=*/0);
  simple.setMomentumSource(&force);
  const auto result =
      simple.solve(mesh, setup->fluid, setup->velocityBoundaries, setup->pressureBoundaries,
                   setup->initialVelocity, setup->initialPressure);
  r.status = simpleStatusName(result.status);
  r.converged = result.converged();
  r.iterations = result.iterations;
  r.finalU = result.finalUResidual;
  r.finalV = result.finalVResidual;
  r.finalP = result.finalPressureResidual;
  r.finalContinuity = result.finalContinuityResidual;
  r.massImbalance = std::abs(result.globalMassImbalance);
  if (result.continuityHistory.size() >= 2 && result.continuityHistory.front() > 0.0) {
    r.continuityContraction =
        std::pow(result.continuityHistory.back() / result.continuityHistory.front(),
                 1.0 / static_cast<Real>(result.continuityHistory.size() - 1));
  }
  r.acceptance = cfd::validation::assessSimpleSolve(result, kMassImbalanceTolerance);
  r.finite = true;
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    r.finite = r.finite && std::isfinite(result.velocity[c].x) &&
               std::isfinite(result.velocity[c].y) && std::isfinite(result.pressure[c]);
  }
  if (!r.finite) {
    r.seconds = secondsSince(start);
    return r;
  }
  const auto continuity = cfd::physics::evaluateContinuity(mesh, result.massFlux);
  for (const auto& cell : mesh.cells()) {
    r.maxCellContinuity = std::max(r.maxCellContinuity,
                                   std::abs(continuity.cellImbalance[cell.id()]) / cell.volume());
  }
  for (const auto& cell : mesh.cells()) {
    r.kineticEnergy +=
        0.5 * dot(result.velocity[cell.id()], result.velocity[cell.id()]) * cell.volume();
  }
  const auto velocityErrors =
      cfd::validation::computeVectorErrorNorms(mesh, result.velocity, exactU);
  r.u = velocityErrors.x;
  r.v = velocityErrors.y;
  r.pressure = cfd::validation::computeGaugeInvariantErrorNorms(mesh, result.pressure, exactP);

  // Temperature, with the production thermal settings (ProjectRunner's).
  cfd::thermal::ThermalSolverSettings thermalSettings;
  thermalSettings.nonOrthogonal =
      cfd::pressure_velocity::nonOrthogonalOptions(setup->solverSettings);
  const auto thermal = cfd::thermal::ThermalSolver{thermalSettings}.solve(
      mesh, *setup->initialTemperature, result.massFlux, *setup->thermal,
      *setup->temperatureBoundaries, heat);
  r.thermalConverged = thermal.converged();
  r.thermalIterations = thermal.iterations;
  r.thermalLinearInitial = thermal.initialResidual;
  r.thermalLinearFinal = thermal.finalResidual;
  switch (thermal.status) {
    case cfd::thermal::ThermalStatus::Converged:
      r.thermalStatus = "Converged";
      break;
    case cfd::thermal::ThermalStatus::MaxIterations:
      r.thermalStatus = "MaxIterations";
      break;
    case cfd::thermal::ThermalStatus::LinearSolveFailure:
      r.thermalStatus = "LinearSolveFailure";
      break;
    case cfd::thermal::ThermalStatus::NonFiniteState:
      r.thermalStatus = "NonFiniteState";
      break;
    case cfd::thermal::ThermalStatus::InvalidConfiguration:
      r.thermalStatus = "InvalidConfiguration";
      break;
  }
  bool temperatureFinite = true;
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    temperatureFinite = temperatureFinite && std::isfinite(thermal.temperature[c]);
  }
  r.finite = r.finite && temperatureFinite;
  if (temperatureFinite) {
    r.temperature = cfd::validation::computeErrorNorms(mesh, thermal.temperature, exactT);
    for (const auto& cell : mesh.cells()) {
      r.meanTemperature += thermal.temperature[cell.id()] * cell.volume();
    }
    // Global discrete energy balance of the returned field: the sum of the
    // assembled equation's residuals (face fluxes telescope) relative to the
    // total source magnitude.
    const auto assembly = cfd::thermal::assembleEnergyEquation(
        mesh, thermal.temperature, result.massFlux, *setup->thermal, *setup->temperatureBoundaries,
        heat, thermalSettings.nonOrthogonal);
    cfd::algebra::Vector t(mesh.numberOfCells());
    for (Index c = 0; c < mesh.numberOfCells(); ++c) t[c] = thermal.temperature[c];
    const auto at = assembly.system.matrix().multiply(t);
    Real residualSum = 0.0;
    Real sourceMagnitude = 0.0;
    for (const auto& cell : mesh.cells()) {
      residualSum += assembly.system.rhs()[cell.id()] - at[cell.id()];
      sourceMagnitude += std::abs(heat[cell.id()]) * cell.volume();
    }
    r.energyImbalance = std::abs(residualSum) / sourceMagnitude;
  }
  r.seconds = secondsSince(start);
  return r;
}

std::string describe(const std::string& label, const Run& r) {
  char buffer[1024];
  if (!r.built) {
    std::snprintf(buffer, sizeof(buffer), "%-22s REJECTED: %.300s", label.c_str(),
                  r.buildError.c_str());
    return buffer;
  }
  const auto& q = r.quality;
  std::snprintf(
      buffer, sizeof(buffer),
      "%-22s %-19s cells %5zu | AR %7.3f nonorth %7.3f skew %7.4f expan %6.3f | %-13s %5zu it "
      "%6.2f s contr %.5f | u L2 %.4e Linf %.4e | v L2 %.4e | p L2 %.4e | T L2 %.4e Linf %.4e (%s, "
      "%zu it, lin %.1e->%.1e) | mass %.1e cell-cont %.1e energy %.1e",
      label.c_str(), cfd::mesh::meshQualityStatusName(q.status), static_cast<std::size_t>(r.cells),
      q.aspectRatio.maximum, q.nonOrthogonality.maximum, q.skewness.maximum,
      q.expansionRatio.count > 0 ? q.expansionRatio.maximum : 1.0, r.status.c_str(),
      static_cast<std::size_t>(r.iterations), r.seconds, r.continuityContraction, r.u.l2, r.u.linf,
      r.v.l2, r.pressure.l2, r.temperature.l2, r.temperature.linf, r.thermalStatus.c_str(),
      static_cast<std::size_t>(r.thermalIterations), r.thermalLinearInitial, r.thermalLinearFinal,
      r.massImbalance, r.maxCellContinuity, r.energyImbalance);
  return buffer;
}

std::string csvHeader() {
  return "family,level,parameter,recipe,built,quality_status,cells,h,"
         "aspect_ratio_max,aspect_ratio_mean,non_orthogonality_max_deg,non_orthogonality_mean_deg,"
         "skewness_max,skewness_mean,expansion_ratio_max,expansion_ratio_mean,cell_area_min,"
         "simple_status,converged,finite,iterations,final_u,final_v,final_p,final_continuity,"
         "continuity_contraction,seconds,"
         "u_l1,u_l2,u_linf,v_l1,v_l2,v_linf,p_l1,p_l2,p_linf,t_l1,t_l2,t_linf,"
         "thermal_status,thermal_iterations,mass_imbalance,max_cell_continuity,energy_imbalance,"
         "kinetic_energy,mean_temperature";
}

std::string csvRow(const std::string& family, const std::string& level, Real parameter,
                   const std::string& recipe, const Run& r) {
  const auto& q = r.quality;
  const bool hasExpansion = q.expansionRatio.count > 0;
  char buffer[2048];
  std::snprintf(
      buffer, sizeof(buffer),
      "%s,%s,%.17g,%s,%d,%s,%zu,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%s,%d,"
      "%d,%zu,%.6e,%.6e,%.6e,%.6e,%.6e,%.3f,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,%.10e,"
      "%.10e,%.10e,%.10e,%s,%zu,%.3e,%.3e,%.3e,%.15e,%.15e",
      family.c_str(), level.c_str(), parameter, recipe.c_str(), r.built ? 1 : 0,
      r.built ? cfd::mesh::meshQualityStatusName(q.status) : "rejected",
      static_cast<std::size_t>(r.cells), r.h, q.aspectRatio.maximum, q.aspectRatio.mean,
      q.nonOrthogonality.maximum, q.nonOrthogonality.mean, q.skewness.maximum, q.skewness.mean,
      hasExpansion ? q.expansionRatio.maximum : 1.0, hasExpansion ? q.expansionRatio.mean : 1.0,
      q.cellArea.minimum, r.status.c_str(), r.converged ? 1 : 0, r.finite ? 1 : 0,
      static_cast<std::size_t>(r.iterations), r.finalU, r.finalV, r.finalP, r.finalContinuity,
      r.continuityContraction, r.seconds, r.u.l1, r.u.l2, r.u.linf, r.v.l1, r.v.l2, r.v.linf,
      r.pressure.l1, r.pressure.l2, r.pressure.linf, r.temperature.l1, r.temperature.l2,
      r.temperature.linf, r.thermalStatus.c_str(), static_cast<std::size_t>(r.thermalIterations),
      r.massImbalance, r.maxCellContinuity, r.energyImbalance, r.kineticEnergy, r.meanTemperature);
  return buffer;
}

}  // namespace cfd::test::meshq

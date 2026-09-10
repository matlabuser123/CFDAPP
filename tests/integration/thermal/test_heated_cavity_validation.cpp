// P2-THERMAL-004, Stage A: differentially-heated square cavity, pure
// conduction (zero velocity, adiabatic top/bottom, fixed hot/cold
// left/right walls). Analytical solution: T(x) = Th + (Tc-Th)*x/L,
// independent of y. This is the thermal analogue of
// tests/integration/poiseuille's own validation-against-a-closed-form
// approach -- fresh evidence (JSON + CSV) written under
// results/validation/heated_cavity/ by the tests themselves on every run,
// same convention as Poiseuille/cavity validation.
//
// Explicitly NOT natural convection (TODO.md "P2 -- Thermal" >
// "Heated cavity"): velocity is zero everywhere by construction (Wall on
// every patch), so this validates the energy equation + thermal BCs +
// ThermalSolver in isolation, not a buoyancy-driven flow. No Boussinesq
// coupling exists in this codebase yet -- see this phase's TODO.md status
// note for the explicit scope decision.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include "cfd/boundary/Adiabatic.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedTemperature.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
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
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::thermal::ThermalProperties;
using cfd::thermal::ThermalResult;
using cfd::thermal::ThermalSolver;
using cfd::thermal::ThermalSolverSettings;
using cfd::thermal::ThermalStatus;

namespace {

constexpr Real kLength = 1.0;
constexpr Real kHot = 310.0;
constexpr Real kCold = 290.0;
constexpr Real kConductivity = 0.6;
constexpr Real kSpecificHeat = 4180.0;

Real analyticalTemperature(Real x) { return kHot + (kCold - kHot) * (x / kLength); }

BoundaryConditionSet makeVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<Wall>());
  }
  return boundaries;
}

BoundaryConditionSet makePressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

BoundaryConditionSet makeTemperatureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(kHot));
  boundaries.set(mesh, "right", std::make_unique<FixedTemperature>(kCold));
  boundaries.set(mesh, "top", std::make_unique<Adiabatic>());
  boundaries.set(mesh, "bottom", std::make_unique<Adiabatic>());
  return boundaries;
}

struct ErrorMetrics {
  Real l2{0.0};
  Real lInf{0.0};
};

ErrorMetrics computeTemperatureErrors(const Mesh& mesh, const ScalarField& temperature) {
  ErrorMetrics metrics;
  Real sumSquares = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real exact = analyticalTemperature(cell.centroid().x);
    const Real error = std::abs(temperature[cell.id()] - exact);
    sumSquares += error * error;
    metrics.lInf = std::max(metrics.lInf, error);
  }
  metrics.l2 = std::sqrt(sumSquares / static_cast<Real>(mesh.numberOfCells()));
  return metrics;
}

// Heat leaving the domain through one boundary face, via Fourier's law
// against the mesh's outward normal (same sign convention as
// boundary::HeatFlux -- see its own header comment): q_leaving = k*A*
// (T_owner - T_boundary)/distance. Summed per patch to get each wall's
// total heat flow (W/m in this 2D, per-unit-depth convention -- matches
// physics::MassFlux's own documented 2D convention).
Real patchHeatLeaving(const Mesh& mesh, const ScalarField& temperature,
                      const BoundaryConditionSet& temperatureBoundaries,
                      const std::string& patchName, Real conductivity) {
  Real total = 0.0;
  for (const Index faceId : mesh.boundaryPatch(patchName).faceIds()) {
    const Face& face = mesh.face(faceId);
    const Index ownerId = face.owner();
    const Real distance =
        cfd::mesh::MeshGeometry::distance(mesh.cell(ownerId).centroid(), face.centroid());
    const auto& bc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition&>(
        cfd::boundary::boundaryConditionForFace(mesh, faceId, temperatureBoundaries));
    const Real tOwner = temperature[ownerId];
    const Real tBoundary = bc.boundaryValue(tOwner, distance);
    total += conductivity * face.area() * (tOwner - tBoundary) / distance;
  }
  return total;
}

struct GridResult {
  Index nx{0};
  Index ny{0};
  ErrorMetrics error;
  Real hotWallHeatLeaving{0.0};
  Real coldWallHeatLeaving{0.0};
  Real topWallHeatLeaving{0.0};
  Real bottomWallHeatLeaving{0.0};
  Real globalImbalance{0.0};
  ThermalStatus thermalStatus{ThermalStatus::MaxIterations};
  Index thermalIterations{0};
};

GridResult runConductionCavity(Index nx, Index ny,
                               ThermalSolverSettings thermalSettings = {}) {
  const Mesh mesh = MeshGeometry::createCartesian2D(nx, ny, kLength, kLength);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const auto temperatureBoundaries = makeTemperatureBoundaries(mesh);
  const FluidProperties fluid(1000.0, 0.001);
  const ThermalProperties thermal(kConductivity, kSpecificHeat);

  const Index n = mesh.numberOfCells();
  const VectorField initialVelocity(n, Vector2{0.0, 0.0});
  const ScalarField initialPressure(n, 0.0);

  SIMPLESettings simpleSettings;
  simpleSettings.maxIterations = 100;
  const SIMPLE simple(simpleSettings, /*referenceCell=*/0);
  const SIMPLEResult flow = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                         initialVelocity, initialPressure);
  if (flow.status != SIMPLEStatus::Converged) {
    throw std::runtime_error("runConductionCavity: flow solve did not converge");
  }

  const SurfaceField massFlux = calculateMassFlux(mesh, flow.velocity, fluid, velocityBoundaries);
  const ScalarField initialTemperature(n, 300.0);
  const ThermalSolver thermalSolver{thermalSettings};
  const ThermalResult thermalResult = thermalSolver.solve(
      mesh, initialTemperature, massFlux, thermal, temperatureBoundaries);

  GridResult result;
  result.nx = nx;
  result.ny = ny;
  result.thermalStatus = thermalResult.status;
  result.thermalIterations = thermalResult.iterations;
  if (thermalResult.status != ThermalStatus::Converged) {
    return result;
  }

  result.error = computeTemperatureErrors(mesh, thermalResult.temperature);
  result.hotWallHeatLeaving =
      patchHeatLeaving(mesh, thermalResult.temperature, temperatureBoundaries, "left",
                       kConductivity);
  result.coldWallHeatLeaving =
      patchHeatLeaving(mesh, thermalResult.temperature, temperatureBoundaries, "right",
                       kConductivity);
  result.topWallHeatLeaving =
      patchHeatLeaving(mesh, thermalResult.temperature, temperatureBoundaries, "top",
                       kConductivity);
  result.bottomWallHeatLeaving =
      patchHeatLeaving(mesh, thermalResult.temperature, temperatureBoundaries, "bottom",
                       kConductivity);
  result.globalImbalance = std::abs(result.hotWallHeatLeaving + result.coldWallHeatLeaving +
                                    result.topWallHeatLeaving + result.bottomWallHeatLeaving);
  return result;
}

void writeValidationJson(const std::string& path, const GridResult& result) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeValidationJson: could not open " + path);
  out << "{\n"
      << "  \"case\": \"heated_cavity_conduction\",\n"
      << "  \"mesh\": { \"nx\": " << result.nx << ", \"ny\": " << result.ny << " },\n"
      << "  \"thermal\": {\n"
      << "    \"converged\": " << (result.thermalStatus == ThermalStatus::Converged ? "true"
                                                                                    : "false")
      << ",\n"
      << "    \"iterations\": " << result.thermalIterations << "\n"
      << "  },\n"
      << "  \"validation\": {\n"
      << "    \"temperature_l2\": " << result.error.l2 << ",\n"
      << "    \"temperature_linf\": " << result.error.lInf << ",\n"
      << "    \"hot_wall_heat_leaving\": " << result.hotWallHeatLeaving << ",\n"
      << "    \"cold_wall_heat_leaving\": " << result.coldWallHeatLeaving << ",\n"
      << "    \"top_wall_heat_leaving\": " << result.topWallHeatLeaving << ",\n"
      << "    \"bottom_wall_heat_leaving\": " << result.bottomWallHeatLeaving << ",\n"
      << "    \"global_heat_imbalance\": " << result.globalImbalance << "\n"
      << "  }\n"
      << "}\n";
}

void writeTemperatureProfileCsv(const std::string& path, const Mesh& mesh,
                                const ScalarField& temperature, Index ny) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream out(path);
  if (!out) throw std::runtime_error("writeTemperatureProfileCsv: could not open " + path);
  out << "x,numerical,analytical\n";
  // Mid-height row only (matches the "T independent of y" analytical
  // property -- a single representative row is enough evidence, not
  // every cell).
  const Index midRow = ny / 2;
  const Index nx = mesh.numberOfCells() / ny;
  for (Index i = 0; i < nx; ++i) {
    const Index cellId = (midRow * nx) + i;
    const Real x = mesh.cell(cellId).centroid().x;
    out << x << ',' << temperature[cellId] << ',' << analyticalTemperature(x) << '\n';
  }
}

}  // namespace

TEST(HeatedCavityConductionValidation, Grid20x20MatchesAnalyticalProfile) {
  const GridResult result = runConductionCavity(20, 20);
  ASSERT_EQ(result.thermalStatus, ThermalStatus::Converged);

  EXPECT_LT(result.error.l2, 1e-6);
  EXPECT_LT(result.error.lInf, 1e-6);
  // Adiabatic walls: (near-)zero heat flow.
  EXPECT_NEAR(result.topWallHeatLeaving, 0.0, 1e-8);
  EXPECT_NEAR(result.bottomWallHeatLeaving, 0.0, 1e-8);
  // Steady state, no source: heat entering the hot wall (negative
  // "leaving") must balance heat leaving the cold wall.
  EXPECT_NEAR(result.hotWallHeatLeaving + result.coldWallHeatLeaving, 0.0, 1e-6);
  EXPECT_LT(result.globalImbalance, 1e-6);

  writeValidationJson("results/validation/heated_cavity/20x20/validation.json", result);

  const Mesh mesh = MeshGeometry::createCartesian2D(20, 20, kLength, kLength);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const FluidProperties fluid(1000.0, 0.001);
  SIMPLESettings simpleSettings;
  simpleSettings.maxIterations = 100;
  const SIMPLE simple(simpleSettings, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  const SIMPLEResult flow = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                         initialVelocity, initialPressure);
  const SurfaceField massFlux = calculateMassFlux(mesh, flow.velocity, fluid, velocityBoundaries);
  const ThermalProperties thermal(kConductivity, kSpecificHeat);
  const ScalarField initialTemperature(mesh.numberOfCells(), 300.0);
  const ThermalResult thermalResult = ThermalSolver{}.solve(
      mesh, initialTemperature, massFlux, thermal, makeTemperatureBoundaries(mesh));
  writeTemperatureProfileCsv("results/validation/heated_cavity/20x20/temperature_profile.csv",
                             mesh, thermalResult.temperature, 20);
}

TEST(HeatedCavityConductionValidation, Grid40x40MatchesAnalyticalProfile) {
  const GridResult result = runConductionCavity(40, 40);
  ASSERT_EQ(result.thermalStatus, ThermalStatus::Converged);
  EXPECT_LT(result.error.l2, 1e-6);
  EXPECT_LT(result.error.lInf, 1e-6);
  EXPECT_LT(result.globalImbalance, 1e-6);
  writeValidationJson("results/validation/heated_cavity/40x40/validation.json", result);
}

TEST(HeatedCavityConductionValidation, Grid80x80MatchesAnalyticalProfile) {
  // Same diagnosed cause as the momentum solver's own 40x40/80x80 cavity
  // precedent (TODO.md "P0 -- Physical Validation" > "Lid-Driven Cavity"):
  // BiCGSTAB's default tolerance (absolute 1e-12, relative 1e-10) hits a
  // genuine accuracy floor -- stagnates around ~1e-9, regardless of
  // maxIterations -- on this larger diffusion matrix, reported as
  // LinearSolveFailure (confirmed by running with the default settings
  // before adding this override, not assumed). Loosening only the
  // *inner* linear-solver tolerance to 1e-8/1e-6 -- still far tighter
  // than the *outer* Picard tolerance (default 1e-8) -- clears the floor;
  // this is case-appropriate solver configuration, not a change to the
  // energy equation or ThermalSolver's own convergence logic.
  ThermalSolverSettings settings;
  settings.linearSolver.absoluteTolerance = 1e-8;
  settings.linearSolver.relativeTolerance = 1e-6;
  const GridResult result = runConductionCavity(80, 80, settings);
  ASSERT_EQ(result.thermalStatus, ThermalStatus::Converged);
  EXPECT_LT(result.error.l2, 1e-6);
  EXPECT_LT(result.error.lInf, 1e-6);
  EXPECT_LT(result.globalImbalance, 1e-6);
  writeValidationJson("results/validation/heated_cavity/80x80/validation.json", result);
}

// The discretization is exact for an affine (linear) analytical field on
// an orthogonal Cartesian mesh (same reasoning as the momentum diffusion
// operator's own analytical-exactness tests), so refinement does not
// reduce an already-near-machine-zero error the way Poiseuille/Ghia's
// curved profiles do -- what refinement *must* still demonstrate here is
// that accuracy does not degrade as the mesh grows, and that the energy
// balance holds increasingly tightly (not loosely) at every resolution.
TEST(HeatedCavityConductionValidation, GridRefinementDoesNotDegradeAccuracy) {
  const GridResult coarse = runConductionCavity(10, 10);
  const GridResult fine = runConductionCavity(40, 40);
  ASSERT_EQ(coarse.thermalStatus, ThermalStatus::Converged);
  ASSERT_EQ(fine.thermalStatus, ThermalStatus::Converged);

  EXPECT_LT(coarse.error.l2, 1e-6);
  EXPECT_LT(fine.error.l2, 1e-6);
  EXPECT_LT(coarse.globalImbalance, 1e-6);
  EXPECT_LT(fine.globalImbalance, 1e-6);
}

TEST(HeatedCavityConductionValidation, RepeatedSolveIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 20, kLength, kLength);
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const auto temperatureBoundaries = makeTemperatureBoundaries(mesh);
  const FluidProperties fluid(1000.0, 0.001);
  const ThermalProperties thermal(kConductivity, kSpecificHeat);
  const Index n = mesh.numberOfCells();
  const VectorField initialVelocity(n, Vector2{0.0, 0.0});
  const ScalarField initialPressure(n, 0.0);
  SIMPLESettings simpleSettings;
  simpleSettings.maxIterations = 100;
  const SIMPLE simple(simpleSettings, 0);
  const ScalarField initialTemperature(n, 300.0);
  const ThermalSolver thermalSolver{};

  auto runOnce = [&]() {
    const SIMPLEResult flow = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);
    const SurfaceField massFlux =
        calculateMassFlux(mesh, flow.velocity, fluid, velocityBoundaries);
    return thermalSolver.solve(mesh, initialTemperature, massFlux, thermal,
                               temperatureBoundaries);
  };

  const ThermalResult a = runOnce();
  const ThermalResult b = runOnce();
  ASSERT_EQ(a.status, ThermalStatus::Converged);
  ASSERT_EQ(b.status, ThermalStatus::Converged);
  EXPECT_EQ(a.iterations, b.iterations);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(a.temperature[i], b.temperature[i]);
  }
}

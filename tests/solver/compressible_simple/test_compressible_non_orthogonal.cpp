// P12-NUM-003 continuation: CompressibleSIMPLE on the geometric pressure-
// correction equation (shared with SIMPLE: pressure_velocity::
// assembleGeometricPressureCorrection) and its non-orthogonal correction
// loop (CompressibleSIMPLESettings::nonOrthogonalCorrections).
//   - Cartesian: the correction changes nothing, bit-for-bit.
//   - Distorted: the genuinely compressible cavity and an open channel
//     (Dirichlet-pressure outlet) converge with mass conservation closed,
//     finite/positive density, and exactly N pressure passes per iteration.
//   - Low-Mach limit: on a DISTORTED mesh, a near-incompressible gas
//     reproduces incompressible SIMPLE on the same mesh (the P12-COMP-002
//     reduction gate, now off the Cartesian mesh).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "DistortedMesh.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressiblePressureCorrection.hpp"
#include "cfd/compressible/CompressibleSIMPLE.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/ContinuityEquation.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::compressible::CompressibleSIMPLE;
using cfd::compressible::CompressibleSIMPLEResult;
using cfd::compressible::CompressibleSIMPLESettings;
using cfd::compressible::CompressibleSIMPLEStatus;
using cfd::compressible::ThermodynamicProperties;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

BoundaryConditionSet cavityVelocity(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::MovingWall>(Vector2{1.0, 0.0}));
  return boundaries;
}

BoundaryConditionSet zeroGradientPressure(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedGradient>(0.0));
  }
  return boundaries;
}

CompressibleSIMPLESettings compressibleSettings(Index corrections) {
  CompressibleSIMPLESettings settings;
  settings.maxIterations = 3000;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.pseudoTimeStep = 1.0;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  settings.nonOrthogonalCorrections = corrections;
  settings.gradientScheme = GradientScheme::LeastSquares;
  return settings;
}

CompressibleSIMPLEResult solveAirCavity(const Mesh& mesh,
                                        const CompressibleSIMPLESettings& settings) {
  const ThermodynamicProperties air(287.05, 1005.0);
  const Real referencePressure = 101325.0;
  const Index n = mesh.numberOfCells();
  const CompressibleSIMPLE solver(settings, air, referencePressure, 0);
  return solver.solve(mesh, 1.8e-5, cavityVelocity(mesh), zeroGradientPressure(mesh),
                      ScalarField(n, 300.0), nullptr, VectorField(n, Vector2{0.0, 0.0}),
                      ScalarField(n, 0.0), ScalarField(n, air.density(referencePressure, 300.0)));
}

}  // namespace

// CartesianCompatibility: the full compressible solve with N = 1 (correction
// on) is bit-identical to N = 0 on a Cartesian mesh, and so is the
// compressible pressure-correction assembly with vs without the
// over-relaxed option (every face is axis-aligned with d exactly parallel).
TEST(CompressibleSIMPLENonOrthogonalTest, CartesianCompatibility) {
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 4, 1.0, 1.0);
  CompressibleSIMPLESettings off = compressibleSettings(0);
  off.maxIterations = 40;
  CompressibleSIMPLESettings on = compressibleSettings(1);
  on.maxIterations = 40;
  const auto a = solveAirCavity(mesh, off);
  const auto b = solveAirCavity(mesh, on);
  ASSERT_EQ(a.iterations, b.iterations);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y);
    EXPECT_EQ(a.pressure[i], b.pressure[i]);
    EXPECT_EQ(a.density[i], b.density[i]);
  }
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    EXPECT_EQ(a.massFlux[f], b.massFlux[f]);
  }

  const ThermodynamicProperties air(287.05, 1005.0);
  const Index n = mesh.numberOfCells();
  const SurfaceField flux(mesh.numberOfFaces(), 1e-3);
  const SurfaceField faceDensity(mesh.numberOfFaces(), 1.17);
  const ScalarField du(n, 0.02);
  const ScalarField dv(n, 0.03);
  const ScalarField pAbs(n, 101325.0);
  const ScalarField temperature(n, 300.0);
  const auto boundaries = zeroGradientPressure(mesh);
  cfd::pressure_velocity::PressureCorrectionOptions options;
  const auto twoPoint = cfd::compressible::assembleCompressiblePressureCorrection(
      mesh, flux, faceDensity, du, dv, pAbs, temperature, air, 1.0, 0, boundaries, options);
  options.nonOrthogonal = true;
  const auto overRelaxed = cfd::compressible::assembleCompressiblePressureCorrection(
      mesh, flux, faceDensity, du, dv, pAbs, temperature, air, 1.0, 0, boundaries, options);
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    EXPECT_EQ(twoPoint.faceCoefficient[f], overRelaxed.faceCoefficient[f]);
  }
  ASSERT_EQ(twoPoint.system.matrix().nonZeros(), overRelaxed.system.matrix().nonZeros());
  for (Index k = 0; k < twoPoint.system.matrix().nonZeros(); ++k) {
    EXPECT_EQ(twoPoint.system.matrix().valuesData()[k],
              overRelaxed.system.matrix().valuesData()[k]);
  }
}

// NonOrthogonalPressureCorrection: the genuinely compressible air cavity on
// a strongly distorted mesh converges with and without the correction;
// mass conservation closes (per cell and globally, impermeable walls carry
// no flux); density stays finite and positive; exactly N pressure passes
// run per iteration.
TEST(CompressibleSIMPLENonOrthogonalTest, NonOrthogonalPressureCorrection) {
  const Index n = 8;
  const Mesh mesh = cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, 0.45 / static_cast<Real>(n));
  for (const Index corrections : {0u, 2u}) {
    const auto result = solveAirCavity(mesh, compressibleSettings(corrections));
    ASSERT_EQ(result.status, CompressibleSIMPLEStatus::Converged) << "N=" << corrections;
    EXPECT_LE(result.finalContinuityResidual, 1e-6);
    EXPECT_LE(result.globalMassImbalance, 1e-6);
    const auto continuity = cfd::physics::evaluateContinuity(mesh, result.massFlux);
    EXPECT_LE(continuity.maxCellImbalance, 1e-6);
    Real maxWallFlux = 0.0;
    for (const auto& patch : mesh.boundaryPatches()) {
      for (const Index faceId : patch.faceIds()) {
        EXPECT_NEAR(result.massFlux[faceId], 0.0, 1e-10);
        maxWallFlux = std::max(maxWallFlux, std::abs(result.massFlux[faceId]));
      }
    }
    Real minDensity = result.density[0];
    Real maxDensity = result.density[0];
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      EXPECT_TRUE(std::isfinite(result.density[i]));
      EXPECT_GT(result.density[i], 0.0);
      minDensity = std::min(minDensity, result.density[i]);
      maxDensity = std::max(maxDensity, result.density[i]);
    }
    EXPECT_EQ(result.pressureCorrectionPasses, result.iterations * std::max<Index>(1, corrections));
    std::printf(
        "\nCompressible air cavity, distorted 8x8 0.45h N=%llu: %llu iterations, final"
        " continuity %.3g, global imbalance %.3g, max cell imbalance %.3g, max |wall flux|"
        " %.3g, density [%.9g, %.9g], pressure passes %llu\n",
        static_cast<unsigned long long>(corrections),
        static_cast<unsigned long long>(result.iterations), result.finalContinuityResidual,
        result.globalMassImbalance, continuity.maxCellImbalance, maxWallFlux, minDensity,
        maxDensity, static_cast<unsigned long long>(result.pressureCorrectionPasses));
  }
}

// An open compressible channel (Inlet -> FixedValue-pressure outlet) on a
// distorted mesh: exercises the Dirichlet-pressure boundary coupling of the
// geometric equation under compressibility; converged mass flow in = out.
TEST(CompressibleSIMPLENonOrthogonalTest, NonOrthogonalOpenChannel) {
  const Index nx = 12;
  const Index ny = 6;
  const Mesh mesh =
      cfd::test::createDistortedQuad2D(nx, ny, 1.0, 0.5, 0.3 * (0.5 / static_cast<Real>(ny)));
  BoundaryConditionSet velocity;
  velocity.set(mesh, "left", std::make_unique<cfd::boundary::Inlet>(Vector2{0.5, 0.0}));
  velocity.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
  velocity.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  velocity.set(mesh, "top", std::make_unique<cfd::boundary::Wall>());
  BoundaryConditionSet pressure;
  pressure.set(mesh, "left", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  pressure.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
  pressure.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  pressure.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));
  const ThermodynamicProperties air(287.05, 1005.0);
  const Real referencePressure = 101325.0;
  const Index n = mesh.numberOfCells();
  const CompressibleSIMPLE solver(compressibleSettings(2), air, referencePressure, 0);
  const auto result = solver.solve(mesh, 1.8e-5, velocity, pressure, ScalarField(n, 300.0), nullptr,
                                   VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0),
                                   ScalarField(n, air.density(referencePressure, 300.0)));
  ASSERT_EQ(result.status, CompressibleSIMPLEStatus::Converged);
  const auto continuity = cfd::physics::evaluateContinuity(mesh, result.massFlux);
  EXPECT_LE(continuity.maxCellImbalance, 1e-6);
  EXPECT_LE(std::abs(continuity.globalNetFlux), 1e-6);
  Real inflow = 0.0;
  Real outflow = 0.0;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      if (patch.name() == "left") inflow += result.massFlux[faceId];
      if (patch.name() == "right") outflow += result.massFlux[faceId];
    }
  }
  std::printf(
      "\nCompressible open channel, distorted 12x6 0.3h N=2: %llu iterations, inlet flux"
      " %.9g, outlet flux %.9g, global net %.3g, max cell imbalance %.3g\n",
      static_cast<unsigned long long>(result.iterations), inflow, outflow, continuity.globalNetFlux,
      continuity.maxCellImbalance);
  EXPECT_LT(inflow, 0.0) << "the inlet must carry inflow (negative owner-oriented flux)";
}

// LowMachRegression on a DISTORTED mesh: a near-incompressible gas (huge
// gas constant -> negligible d(rho)/dp) reproduces incompressible SIMPLE on
// the same distorted mesh with the same correction settings -- the
// P12-COMP-002 reduction gate, off the Cartesian mesh.
TEST(CompressibleSIMPLENonOrthogonalTest, LowMachRegression) {
  const Index nCells = 6;
  const Mesh mesh =
      cfd::test::createDistortedQuad2D(nCells, nCells, 1.0, 1.0, 0.3 / static_cast<Real>(nCells));
  const Index n = mesh.numberOfCells();
  const Real rho = 1.0;
  const Real mu = 0.01;

  cfd::pressure_velocity::SIMPLESettings incompressible;
  incompressible.maxIterations = 3000;
  incompressible.velocityTolerance = 1e-8;
  incompressible.pressureTolerance = 1e-8;
  incompressible.continuityTolerance = 1e-8;
  incompressible.momentumSolver.maxIterations = 500;
  incompressible.momentumSolver.absoluteTolerance = 1e-12;
  incompressible.momentumSolver.relativeTolerance = 1e-10;
  incompressible.pressureSolver.maxIterations = 2000;
  incompressible.pressureSolver.absoluteTolerance = 1e-12;
  incompressible.pressureSolver.relativeTolerance = 1e-10;
  incompressible.nonOrthogonalCorrections = 2;
  incompressible.gradientScheme = GradientScheme::LeastSquares;
  const cfd::pressure_velocity::SIMPLE simple(incompressible, 0);
  const auto reference = simple.solve(mesh, cfd::physics::FluidProperties(rho, mu),
                                      cavityVelocity(mesh), zeroGradientPressure(mesh),
                                      VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0));
  ASSERT_EQ(reference.status, cfd::pressure_velocity::SIMPLEStatus::Converged);

  const Real gasConstant = 1.0e10;
  const Real temperature = 300.0;
  const ThermodynamicProperties gas(gasConstant, 2.0e10);
  CompressibleSIMPLESettings settings = compressibleSettings(2);
  settings.velocityTolerance = 1e-8;
  settings.pressureTolerance = 1e-8;
  settings.continuityTolerance = 1e-8;
  settings.momentumSolver = incompressible.momentumSolver;
  settings.pressureSolver = incompressible.pressureSolver;
  const CompressibleSIMPLE compressible(settings, gas, rho * gasConstant * temperature, 0);
  const auto result = compressible.solve(
      mesh, mu, cavityVelocity(mesh), zeroGradientPressure(mesh), ScalarField(n, temperature),
      nullptr, VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0), ScalarField(n, rho));
  ASSERT_EQ(result.status, CompressibleSIMPLEStatus::Converged);
  Real maxVelocityDifference = 0.0;
  Real maxDensityDeviation = 0.0;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(result.velocity[i].x, reference.velocity[i].x, 1e-4) << "cell " << i;
    EXPECT_NEAR(result.velocity[i].y, reference.velocity[i].y, 1e-4) << "cell " << i;
    EXPECT_NEAR(result.density[i], rho, 1e-6) << "cell " << i;
    maxVelocityDifference =
        std::max({maxVelocityDifference, std::abs(result.velocity[i].x - reference.velocity[i].x),
                  std::abs(result.velocity[i].y - reference.velocity[i].y)});
    maxDensityDeviation = std::max(maxDensityDeviation, std::abs(result.density[i] - rho));
  }
  std::printf(
      "\nLow-Mach limit, distorted 6x6 0.3h N=2 LS: max |u_compressible - u_SIMPLE| %.3g,"
      " max |rho - 1| %.3g (iterations: SIMPLE %llu, compressible %llu)\n",
      maxVelocityDifference, maxDensityDeviation,
      static_cast<unsigned long long>(reference.iterations),
      static_cast<unsigned long long>(result.iterations));
}

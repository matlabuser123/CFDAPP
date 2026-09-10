// P3-PHYS-001 Phase 8: the coupled temperature -> buoyancy -> momentum ->
// pressure/velocity -> temperature regression. Composes SIMPLE (with the
// new optional temperature/buoyancy pair, P3-PHYS-001) and ThermalSolver
// in a small, explicit outer Picard loop this test owns directly -- the
// same "CaseBuilder builds data, the caller orchestrates the solve" split
// this project already uses elsewhere (SIMPLE.hpp's own header comment on
// why full two-way coupling is not built into SIMPLE itself). Not a
// benchmark-quality natural-convection validation (no Rayleigh/Nusselt
// comparison against published data -- that is explicitly out of this
// task's scope, left to a future P3-PHYS-002-style task per the task
// spec's own Phase 8/9 instructions); this proves the *coupling
// mechanism* itself is correct, deterministic, and sign-consistent.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

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
using cfd::fields::SurfaceField;
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

namespace {

constexpr Index kNx = 8;
constexpr Index kNy = 8;
constexpr Real kLength = 0.1;
constexpr Real kHeight = 0.1;
constexpr Real kTRef = 300.0;
constexpr Real kHotWallTemperature = 305.0;
constexpr Real kColdWallTemperature = 295.0;
// Small enough that the buoyancy-driven velocities stay modest on this
// coarse mesh (diagnosed empirically, temporary standalone compilation,
// same precedent as every other numerical task in this codebase) --
// this is a coupling-mechanism regression, not a Rayleigh-number-matched
// physical case, so beta is chosen for a well-behaved coupled solve
// rather than any particular real fluid.
constexpr Real kBeta = 0.02;

Mesh makeCavityMesh() { return MeshGeometry::createCartesian2D(kNx, kNy, kLength, kHeight); }

BoundaryConditionSet makeVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

BoundaryConditionSet makePressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

// Differentially heated: left wall hot, right wall cold, top/bottom
// adiabatic -- the classic natural-convection cavity setup (task Phase 9
// Option B).
BoundaryConditionSet makeTemperatureBoundaries(const Mesh& mesh, Real hot, Real cold) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedTemperature>(hot));
  boundaries.set(mesh, "right", std::make_unique<FixedTemperature>(cold));
  boundaries.set(mesh, "top", std::make_unique<Adiabatic>());
  boundaries.set(mesh, "bottom", std::make_unique<Adiabatic>());
  return boundaries;
}

SIMPLESettings makeFlowSettings() {
  SIMPLESettings settings;
  settings.maxIterations = 2000;
  settings.velocityRelaxation = 0.5;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-7;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-7;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  return settings;
}

struct CoupledResult {
  SIMPLEResult flow;
  ScalarField temperature;
};

// One small, explicit outer Picard loop: temperature -> buoyancy ->
// SIMPLE (momentum/pressure/velocity) -> mass flux -> ThermalSolver ->
// new temperature -> repeat. `outerIterations` is fixed (not adaptively
// converged) deliberately -- this is a coupling-mechanism regression
// with a known, bounded, deterministic cost, not a production natural-
// convection solve (see this file's own header comment).
CoupledResult runCoupledCavity(const Mesh& mesh, const FluidProperties& fluid,
                               const ThermalProperties& thermalProps, Real beta, Vector2 gravity,
                               Real hotWallTemperature, Real coldWallTemperature,
                               Index outerIterations) {
  const auto velocityBoundaries = makeVelocityBoundaries(mesh);
  const auto pressureBoundaries = makePressureBoundaries(mesh);
  const auto temperatureBoundaries =
      makeTemperatureBoundaries(mesh, hotWallTemperature, coldWallTemperature);

  ScalarField temperature(mesh.numberOfCells(), kTRef);
  VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  ScalarField pressure(mesh.numberOfCells(), 0.0);
  SIMPLEResult flowResult;

  const ThermalSolver thermalSolver{};

  for (Index outer = 0; outer < outerIterations; ++outer) {
    const BoussinesqBuoyancy buoyancy(fluid.density(), beta, kTRef, gravity);
    const SIMPLE simple(makeFlowSettings(), 0, nullptr, &temperature, &buoyancy);
    flowResult =
        simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, velocity, pressure);
    if (flowResult.status != SIMPLEStatus::Converged) break;
    velocity = flowResult.velocity;
    pressure = flowResult.pressure;

    const ThermalResult thermalResult = thermalSolver.solve(mesh, temperature, flowResult.massFlux,
                                                            thermalProps, temperatureBoundaries);
    if (thermalResult.status != ThermalStatus::Converged) break;
    temperature = thermalResult.temperature;
  }

  return CoupledResult{flowResult, temperature};
}

Real maxAbsVelocityComponent(const VectorField& velocity) {
  Real maxAbs = 0.0;
  for (Index i = 0; i < velocity.size(); ++i) {
    maxAbs = std::max({maxAbs, std::abs(velocity[i].x), std::abs(velocity[i].y)});
  }
  return maxAbs;
}

}  // namespace

// --- 1. Nonuniform temperature produces nonzero velocity --------------------

TEST(BoussinesqCouplingTest, DifferentiallyHeatedCavityProducesNonzeroVelocity) {
  const Mesh mesh = makeCavityMesh();
  const FluidProperties fluid(1.0, 0.01);
  const ThermalProperties thermalProps(0.6, 4180.0);

  const CoupledResult result =
      runCoupledCavity(mesh, fluid, thermalProps, kBeta, Vector2{0.0, -9.81}, kHotWallTemperature,
                       kColdWallTemperature, 6);

  ASSERT_EQ(result.flow.status, SIMPLEStatus::Converged);
  EXPECT_GT(maxAbsVelocityComponent(result.flow.velocity), 1e-4)
      << "buoyancy in a fully closed (all-Wall) cavity is the *only* possible momentum "
         "source here -- a near-zero velocity means the coupling never actually drove any "
         "flow";
  // The temperature field must have actually evolved away from the
  // uniform T_ref initial guess (proves ThermalSolver's own output fed
  // back into the loop, not silently ignored).
  bool temperatureVaries = false;
  for (Index i = 1; i < result.temperature.size(); ++i) {
    if (result.temperature[i] != result.temperature[0]) {
      temperatureVaries = true;
      break;
    }
  }
  EXPECT_TRUE(temperatureVaries);
}

// --- 2. Uniform T = T_ref (equal wall temperatures) gives zero buoyancy ----

TEST(BoussinesqCouplingTest, EqualWallTemperaturesGiveZeroVelocity) {
  const Mesh mesh = makeCavityMesh();
  const FluidProperties fluid(1.0, 0.01);
  const ThermalProperties thermalProps(0.6, 4180.0);

  // Both walls at T_ref: the energy equation's own steady solution is
  // then the uniform T_ref field everywhere (no gradient to drive
  // conduction), so buoyancy stays exactly zero every outer iteration --
  // the cavity must remain at rest.
  const CoupledResult result =
      runCoupledCavity(mesh, fluid, thermalProps, kBeta, Vector2{0.0, -9.81}, kTRef, kTRef, 6);

  ASSERT_EQ(result.flow.status, SIMPLEStatus::Converged);
  for (Index i = 0; i < result.flow.velocity.size(); ++i) {
    EXPECT_DOUBLE_EQ(result.flow.velocity[i].x, 0.0) << "cell " << i;
    EXPECT_DOUBLE_EQ(result.flow.velocity[i].y, 0.0) << "cell " << i;
  }
}

// --- 3. Reversing gravity reverses the circulation direction ---------------

TEST(BoussinesqCouplingTest, ReversingGravityReversesCirculationDirection) {
  const Mesh mesh = makeCavityMesh();
  const FluidProperties fluid(1.0, 0.01);
  const ThermalProperties thermalProps(0.6, 4180.0);

  const CoupledResult downward =
      runCoupledCavity(mesh, fluid, thermalProps, kBeta, Vector2{0.0, -9.81}, kHotWallTemperature,
                       kColdWallTemperature, 6);
  const CoupledResult upward =
      runCoupledCavity(mesh, fluid, thermalProps, kBeta, Vector2{0.0, 9.81}, kHotWallTemperature,
                       kColdWallTemperature, 6);
  ASSERT_EQ(downward.flow.status, SIMPLEStatus::Converged);
  ASSERT_EQ(upward.flow.status, SIMPLEStatus::Converged);

  // Sample the vertical velocity in the column of cells immediately next
  // to the hot (left) wall: with real downward gravity, hot fluid there
  // must rise (v > 0); with gravity artificially flipped upward, the
  // same near-wall fluid must instead sink (v < 0) -- the solver-level
  // manifestation of BoussinesqBuoyancyTest's own sign-convention proof.
  const Index nearHotWallCell = 0 * kNx + 0;  // row 0, column 0 (bottom-left corner region).
  const Index midHeightNearHotWallCell = (kNy / 2) * kNx + 0;
  EXPECT_GT(downward.flow.velocity[midHeightNearHotWallCell].y, 0.0)
      << "hot fluid near the hot wall must rise under real downward gravity";
  EXPECT_LT(upward.flow.velocity[midHeightNearHotWallCell].y, 0.0)
      << "the same hot fluid must sink once gravity is artificially reversed";
  (void)nearHotWallCell;
}

// --- 4. beta = 0 recovers the non-buoyant (at-rest) solution ---------------

TEST(BoussinesqCouplingTest, ZeroBetaRecoversNonBuoyantAtRestSolution) {
  const Mesh mesh = makeCavityMesh();
  const FluidProperties fluid(1.0, 0.01);
  const ThermalProperties thermalProps(0.6, 4180.0);

  const CoupledResult result = runCoupledCavity(mesh, fluid, thermalProps, 0.0, Vector2{0.0, -9.81},
                                                kHotWallTemperature, kColdWallTemperature, 6);

  ASSERT_EQ(result.flow.status, SIMPLEStatus::Converged);
  // No lid, no inlet/outlet, and now no buoyancy either -- momentum has
  // no source term anywhere, so the fully-closed cavity must stay at
  // rest exactly, even though the temperature field itself still
  // develops its own (purely conductive, one-way) profile.
  for (Index i = 0; i < result.flow.velocity.size(); ++i) {
    EXPECT_DOUBLE_EQ(result.flow.velocity[i].x, 0.0) << "cell " << i;
    EXPECT_DOUBLE_EQ(result.flow.velocity[i].y, 0.0) << "cell " << i;
  }
}

// --- 5. Determinism ----------------------------------------------------------

TEST(BoussinesqCouplingTest, RepeatedCoupledSolveIsBitIdentical) {
  const Mesh mesh = makeCavityMesh();
  const FluidProperties fluid(1.0, 0.01);
  const ThermalProperties thermalProps(0.6, 4180.0);

  const CoupledResult a = runCoupledCavity(mesh, fluid, thermalProps, kBeta, Vector2{0.0, -9.81},
                                           kHotWallTemperature, kColdWallTemperature, 6);
  const CoupledResult b = runCoupledCavity(mesh, fluid, thermalProps, kBeta, Vector2{0.0, -9.81},
                                           kHotWallTemperature, kColdWallTemperature, 6);

  ASSERT_EQ(a.flow.status, SIMPLEStatus::Converged);
  ASSERT_EQ(a.flow.status, b.flow.status);
  ASSERT_EQ(a.flow.iterations, b.flow.iterations);
  for (Index i = 0; i < a.flow.velocity.size(); ++i) {
    EXPECT_EQ(a.flow.velocity[i].x, b.flow.velocity[i].x) << "cell " << i;
    EXPECT_EQ(a.flow.velocity[i].y, b.flow.velocity[i].y) << "cell " << i;
    EXPECT_EQ(a.flow.pressure[i], b.flow.pressure[i]) << "cell " << i;
    EXPECT_EQ(a.temperature[i], b.temperature[i]) << "cell " << i;
  }
}

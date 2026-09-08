// P2 -- Transient CFD, Part 2 (transient validation cases): startup
// planar Poiseuille flow -- an impulsively-started (U=0 initial
// condition) pressure-driven channel, run through the real PISO/
// TransientSolver path, developing toward the same closed-form
// steady-state parabolic profile the already-validated steady SIMPLE
// case matches (test_poiseuille_validation.cpp). Reuses that file's own
// geometry/boundary-condition convention and PoiseuilleValidationUtils.hpp
// directly -- no new numerical formulas, no PISO mathematics changed.
//
// Same 64x8 grid and physical parameters as the steady suite's own
// smallest (fastest, already-validated) grid: H=1, L=8H, rho=1, mu=0.1,
// uniform inlet Uavg=1 => Re=10.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

#include "PoiseuilleValidationUtils.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/solver/TimeController.hpp"
#include "cfd/solver/TransientSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::LinearSolverSettings;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::PISO;
using cfd::pressure_velocity::PISOSettings;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::solver::TimeController;
using cfd::solver::TransientResult;
using cfd::solver::TransientSolver;
using cfd::solver::TransientState;
using cfd::solver::TransientStatus;

namespace {

constexpr Real kChannelHeight = 1.0;
constexpr Real kChannelLength = 8.0;
constexpr Index kNx = 64;
constexpr Index kNy = 8;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.1;
constexpr Real kMeanVelocity = 1.0;

constexpr Real kProfileStationX = 0.75 * kChannelLength;
constexpr Real kPressureStationX1 = 0.40 * kChannelLength;
constexpr Real kPressureStationX2 = 0.75 * kChannelLength;

BoundaryConditionSet makeChannelVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{kMeanVelocity, 0.0}));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

BoundaryConditionSet makeChannelPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  return boundaries;
}

PISOSettings makePisoSettings() {
  LinearSolverSettings momentum;
  momentum.maxIterations = 1000;
  momentum.absoluteTolerance = 1e-11;
  momentum.relativeTolerance = 1e-9;
  LinearSolverSettings pressure;
  pressure.maxIterations = 3000;
  pressure.absoluteTolerance = 1e-8;
  pressure.relativeTolerance = 1e-6;
  PISOSettings settings;
  settings.momentumSolver = momentum;
  settings.pressureSolver = pressure;
  return settings;
}

struct ChannelFixture {
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  FluidProperties fluid;
};

ChannelFixture makeChannelFixture() {
  Mesh mesh = MeshGeometry::createCartesian2D(kNx, kNy, kChannelLength, kChannelHeight);
  auto velocityBoundaries = makeChannelVelocityBoundaries(mesh);
  auto pressureBoundaries = makeChannelPressureBoundaries(mesh);
  return ChannelFixture{std::move(mesh), std::move(velocityBoundaries),
                        std::move(pressureBoundaries), FluidProperties(kDensity, kViscosity)};
}

TransientState restState(const Mesh& mesh, const FluidProperties& fluid,
                         const BoundaryConditionSet& velocityBoundaries) {
  TransientState state;
  state.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  state.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  state.massFlux = calculateMassFlux(mesh, state.velocity, fluid, velocityBoundaries);
  return state;
}

// Runs a fresh startup channel (own mesh/fixture) from rest to
// `finalTime` at fixed `dt` -- a completely independent run, not a copy
// of any caller's own state, for genuine repeated-run determinism
// checks.
TransientResult runStartupChannel(Real dt, Real finalTime) {
  const ChannelFixture channel = makeChannelFixture();
  const PISO piso(channel.mesh, channel.fluid, channel.velocityBoundaries,
                  channel.pressureBoundaries, makePisoSettings(), /*referenceCell=*/0);
  const TransientSolver solver(piso, /*cflFailAbove=*/1e6);  // diagnostic-only in this task's scope
  return solver.solve(restState(channel.mesh, channel.fluid, channel.velocityBoundaries),
                      TimeController(0.0, finalTime, dt, 1'000'000));
}

}  // namespace

TEST(TransientPoiseuilleTest, StartupChannelReachesFiniteConservativeStateAndDevelopsProfile) {
  const ChannelFixture channel = makeChannelFixture();
  const PISO piso(channel.mesh, channel.fluid, channel.velocityBoundaries,
                  channel.pressureBoundaries, makePisoSettings(), /*referenceCell=*/0);
  const TransientSolver solver(piso, /*cflFailAbove=*/1e6);

  const Real dt = 0.02;
  const Real finalTime = 4.0;  // ~4 diffusive times (H^2/nu = 1/0.1 = 10) worth of startup
  const TransientResult result =
      solver.solve(restState(channel.mesh, channel.fluid, channel.velocityBoundaries),
                   TimeController(0.0, finalTime, dt, 1'000'000));

  // TransientSolver only ever returns Completed if every accepted step
  // along the way passed its own independent finite-state check
  // (TODO.md P2 section 15/51) -- reaching Completed here is itself
  // evidence "finite at every accepted timestep" held throughout, not
  // only at the end. Still check the final state explicitly too.
  ASSERT_EQ(result.status, TransientStatus::Completed)
      << "startup channel did not complete (history size " << result.history.size() << ")";
  for (Index i = 0; i < result.finalState.velocity.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.finalState.velocity[i].x));
    EXPECT_TRUE(std::isfinite(result.finalState.velocity[i].y));
    EXPECT_TRUE(std::isfinite(result.finalState.pressure[i]));
  }
  for (Index i = 0; i < result.finalState.massFlux.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.finalState.massFlux[i]));
  }

  // Wall no-slip: zero normal flux through every top/bottom wall face
  // (the wall-impermeability invariant PISO-B onward already established
  // for a single predictor step, still holding after a full startup
  // transient). Inlet/outlet mass conservation, evaluated from the
  // authoritative corrected F.
  Real maxWallFlux = 0.0;
  Real inletFlux = 0.0;
  Real outletFlux = 0.0;
  for (const auto& patch : channel.mesh.boundaryPatches()) {
    if (patch.name() == "top" || patch.name() == "bottom") {
      for (const Index faceId : patch.faceIds()) {
        maxWallFlux = std::max(maxWallFlux, std::abs(result.finalState.massFlux[faceId]));
      }
    } else if (patch.name() == "left") {
      for (const Index faceId : patch.faceIds()) inletFlux += result.finalState.massFlux[faceId];
    } else if (patch.name() == "right") {
      for (const Index faceId : patch.faceIds()) outletFlux += result.finalState.massFlux[faceId];
    }
  }
  EXPECT_LT(maxWallFlux, 1e-9) << "channel is leaking mass through a wall";
  EXPECT_NEAR(inletFlux + outletFlux, 0.0, 1e-6);
  EXPECT_LT(result.history.back().massImbalance, 1e-6);

  // The velocity profile at t=finalTime should already be close to the
  // analytical fully-developed parabola -- looser than the steady,
  // iterated-to-convergence SIMPLE tolerance (0.02), since a finite
  // startup time is not the same as SIMPLE's own asymptotic convergence.
  const std::string outputDir = "results/validation/transient_poiseuille";
  std::filesystem::create_directories(outputDir);
  const auto profile = cfd::validation::extractVerticalProfileU(
      channel.mesh, kNx, kNy, result.finalState.velocity, kProfileStationX, kChannelHeight);
  const auto velocityError = cfd::validation::computeVelocityProfileErrors(
      profile, kChannelHeight, kMeanVelocity, outputDir + "/startup_profile.csv");
  EXPECT_LT(velocityError.l2, 0.05) << "startup profile has not developed toward the parabola";

  // Deterministic timestep history: bit-identical on a repeated run.
  const TransientResult repeatResult = runStartupChannel(dt, finalTime);
  ASSERT_EQ(repeatResult.status, TransientStatus::Completed);
  ASSERT_EQ(result.history.size(), repeatResult.history.size());
  for (std::size_t i = 0; i < result.history.size(); ++i) {
    EXPECT_EQ(result.history[i].time, repeatResult.history[i].time) << "step " << i;
    EXPECT_EQ(result.history[i].step, repeatResult.history[i].step) << "step " << i;
  }
  for (Index i = 0; i < result.finalState.pressure.size(); ++i) {
    EXPECT_EQ(result.finalState.pressure[i], repeatResult.finalState.pressure[i]) << "cell " << i;
  }
}

// Steady-limit equivalence (explicit regression, TODO.md P2 -- transient
// validation): the long-time transient PISO solution above approaches
// the already-validated steady SIMPLE solution for the identical case.
TEST(TransientPoiseuilleTest, LongTimeTransientApproachesValidatedSteadySimple) {
  const ChannelFixture channel = makeChannelFixture();

  SIMPLESettings simpleSettings;
  simpleSettings.maxIterations = 6000;
  simpleSettings.velocityRelaxation = 0.7;
  simpleSettings.pressureRelaxation = 0.3;
  simpleSettings.velocityTolerance = 2e-5;
  simpleSettings.pressureTolerance = 5e-4;
  simpleSettings.continuityTolerance = 1e-6;
  simpleSettings.momentumSolver.maxIterations = 500;
  simpleSettings.momentumSolver.absoluteTolerance = 1e-10;
  simpleSettings.momentumSolver.relativeTolerance = 1e-8;
  simpleSettings.pressureSolver.maxIterations = 2000;
  simpleSettings.pressureSolver.absoluteTolerance = 1e-8;
  simpleSettings.pressureSolver.relativeTolerance = 1e-6;
  const SIMPLE simple(simpleSettings, /*referenceCell=*/0);
  const SIMPLEResult steady = simple.solve(
      channel.mesh, channel.fluid, channel.velocityBoundaries, channel.pressureBoundaries,
      VectorField(channel.mesh.numberOfCells(), Vector2{0.0, 0.0}),
      ScalarField(channel.mesh.numberOfCells(), 0.0));
  ASSERT_EQ(steady.status, SIMPLEStatus::Converged);

  const PISO piso(channel.mesh, channel.fluid, channel.velocityBoundaries,
                  channel.pressureBoundaries, makePisoSettings(), /*referenceCell=*/0);
  const TransientSolver solver(piso, /*cflFailAbove=*/1e6);
  const Real dt = 0.02;
  const Real finalTime = 8.0;  // longer than the profile-shape test above
  const TransientResult transient =
      solver.solve(restState(channel.mesh, channel.fluid, channel.velocityBoundaries),
                   TimeController(0.0, finalTime, dt, 1'000'000));
  ASSERT_EQ(transient.status, TransientStatus::Completed);

  // Velocity: cell-by-cell L2 relative to the steady field's own norm.
  Real diffSumSquares = 0.0;
  Real steadySumSquares = 0.0;
  for (Index i = 0; i < channel.mesh.numberOfCells(); ++i) {
    const Real dux = transient.finalState.velocity[i].x - steady.velocity[i].x;
    const Real duy = transient.finalState.velocity[i].y - steady.velocity[i].y;
    diffSumSquares += dux * dux + duy * duy;
    steadySumSquares +=
        steady.velocity[i].x * steady.velocity[i].x + steady.velocity[i].y * steady.velocity[i].y;
  }
  const Real velocityRelativeL2 = std::sqrt(diffSumSquares / steadySumSquares);
  EXPECT_LT(velocityRelativeL2, 0.05)
      << "transient PISO's long-time velocity has not approached the steady SIMPLE solution";

  // Pressure gradient (gauge-invariant -- avoids SIMPLE/PISO's independent
  // pressure reference-level conventions) instead of raw pressure values.
  const Real pGradTransient = cfd::validation::numericalPressureGradient(
      channel.mesh, kNx, kNy, transient.finalState.pressure, kPressureStationX1,
      kPressureStationX2);
  const Real pGradSteady = cfd::validation::numericalPressureGradient(
      channel.mesh, kNx, kNy, steady.pressure, kPressureStationX1, kPressureStationX2);
  EXPECT_NEAR(pGradTransient, pGradSteady, 0.05 * std::abs(pGradSteady));

  // Authoritative flux / continuity / global mass imbalance.
  Real maxFluxDiff = 0.0;
  for (Index i = 0; i < channel.mesh.numberOfFaces(); ++i) {
    maxFluxDiff =
        std::max(maxFluxDiff, std::abs(transient.finalState.massFlux[i] - steady.massFlux[i]));
  }
  EXPECT_LT(maxFluxDiff, 0.1) << "authoritative face flux has not approached the steady solution";
  EXPECT_LT(transient.history.back().massImbalance, 1e-6);
  EXPECT_LT(steady.globalMassImbalance, 1e-6);
}

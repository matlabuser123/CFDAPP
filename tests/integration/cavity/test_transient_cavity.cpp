// P2 -- Transient CFD, Part 2 (transient validation cases): an
// impulsively-started (U=0 initial condition, lid instantaneously set to
// U_lid for t>0) lid-driven cavity at Re=100, run through the real PISO/
// TransientSolver path, developing toward the same primary recirculation
// the already-validated steady SIMPLE cavity matches against Ghia, Ghia &
// Shin (1982) (test_cavity_ghia.cpp). Reuses that file's own geometry/
// boundary-condition convention and CavityValidationUtils.hpp/GhiaRe100.hpp
// directly -- no new numerical formulas, no PISO mathematics changed.
//
// Same 20x20 grid and physical parameters as the steady suite's own
// smallest (fastest, already-validated) grid: L=1, rho=1, mu=0.01,
// U_lid=1 => Re=100.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

#include "CavityValidationUtils.hpp"
#include "GhiaRe100.hpp"
#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
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
using cfd::boundary::MovingWall;
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

constexpr Index kN = 20;
constexpr Real kLidVelocity = 1.0;
constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 0.01;  // Re = rho*U*L/mu = 100 for L=1.

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{kLidVelocity, 0.0}));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
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

struct CavityFixture {
  Mesh mesh;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  FluidProperties fluid;
};

CavityFixture makeCavityFixture() {
  Mesh mesh = MeshGeometry::createCartesian2D(kN, kN, 1.0, 1.0);
  auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  return CavityFixture{std::move(mesh), std::move(velocityBoundaries),
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

TransientResult runImpulsiveCavity(Real dt, Real finalTime) {
  const CavityFixture cavity = makeCavityFixture();
  const PISO piso(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                  makePisoSettings(), /*referenceCell=*/0);
  const TransientSolver solver(piso, /*cflFailAbove=*/1e6);
  return solver.solve(restState(cavity.mesh, cavity.fluid, cavity.velocityBoundaries),
                      TimeController(0.0, finalTime, dt, 1'000'000));
}

}  // namespace

TEST(TransientCavityTest, ImpulsiveStartReachesFiniteConservativeStateAndDevelopsRecirculation) {
  const CavityFixture cavity = makeCavityFixture();
  const PISO piso(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                  makePisoSettings(), /*referenceCell=*/0);
  const TransientSolver solver(piso, /*cflFailAbove=*/1e6);

  const Real dt = 0.02;
  const Real finalTime = 6.0;  // several lid-driven convective times (L/U_lid = 1)
  const TransientResult result =
      solver.solve(restState(cavity.mesh, cavity.fluid, cavity.velocityBoundaries),
                   TimeController(0.0, finalTime, dt, 1'000'000));

  // As with the transient Poiseuille case: Completed itself is evidence
  // every accepted step passed TransientSolver's own independent finite
  // check throughout, not only at the end.
  ASSERT_EQ(result.status, TransientStatus::Completed)
      << "impulsive cavity did not complete (history size " << result.history.size() << ")";
  for (Index i = 0; i < result.finalState.velocity.size(); ++i) {
    EXPECT_TRUE(std::isfinite(result.finalState.velocity[i].x));
    EXPECT_TRUE(std::isfinite(result.finalState.velocity[i].y));
    EXPECT_TRUE(std::isfinite(result.finalState.pressure[i]));
  }

  // Wall no-slip / impermeability: zero normal flux through every wall
  // (all four sides are Wall/MovingWall -- a fully closed cavity).
  Real maxWallFlux = 0.0;
  for (const auto& patch : cavity.mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      maxWallFlux = std::max(maxWallFlux, std::abs(result.finalState.massFlux[faceId]));
    }
  }
  EXPECT_LT(maxWallFlux, 1e-9) << "cavity is leaking mass through a wall";
  EXPECT_LT(result.history.back().massImbalance, 1e-6);

  // Primary recirculation has developed: the vertical centerline u-profile
  // and horizontal centerline v-profile compared against Ghia's Re=100
  // reference. Looser than the steady, iterated-to-convergence SIMPLE
  // tolerance, since a finite startup time is not SIMPLE's own asymptotic
  // convergence.
  const std::string outputDir = "results/validation/transient_cavity";
  std::filesystem::create_directories(outputDir);
  const auto profileU = cfd::validation::extractVerticalProfileU(
      cavity.mesh, kN, kN, result.finalState.velocity, 0.5, kLidVelocity);
  const auto profileV = cfd::validation::extractHorizontalProfileV(cavity.mesh, kN, kN,
                                                                   result.finalState.velocity, 0.5);
  const auto errorsU =
      cfd::validation::computeGhiaErrors(profileU, cfd::validation::ghia_re100::kCenterlineU,
                                         outputDir + "/u_centerline.csv", "y", "u");
  const auto errorsV =
      cfd::validation::computeGhiaErrors(profileV, cfd::validation::ghia_re100::kCenterlineV,
                                         outputDir + "/v_centerline.csv", "x", "v");
  EXPECT_LT(errorsU.l2, 0.15) << "startup u-centerline has not developed the primary recirculation";
  EXPECT_LT(errorsV.l2, 0.15) << "startup v-centerline has not developed the primary recirculation";

  // Deterministic timestep history: bit-identical on a repeated run.
  const TransientResult repeatResult = runImpulsiveCavity(dt, finalTime);
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
// validation): the long-time transient PISO solution approaches the
// already-validated steady SIMPLE solution for the identical cavity.
TEST(TransientCavityTest, LongTimeTransientApproachesValidatedSteadySimple) {
  const CavityFixture cavity = makeCavityFixture();

  SIMPLESettings simpleSettings;
  simpleSettings.maxIterations = 6000;
  simpleSettings.velocityRelaxation = 0.7;
  simpleSettings.pressureRelaxation = 0.3;
  simpleSettings.velocityTolerance = 1e-6;
  simpleSettings.pressureTolerance = 1e-6;
  simpleSettings.continuityTolerance = 1e-6;
  simpleSettings.momentumSolver.maxIterations = 500;
  simpleSettings.momentumSolver.absoluteTolerance = 1e-10;
  simpleSettings.momentumSolver.relativeTolerance = 1e-8;
  simpleSettings.pressureSolver.maxIterations = 2000;
  simpleSettings.pressureSolver.absoluteTolerance = 1e-10;
  simpleSettings.pressureSolver.relativeTolerance = 1e-8;
  const SIMPLE simple(simpleSettings, /*referenceCell=*/0);
  const SIMPLEResult steady =
      simple.solve(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                   VectorField(cavity.mesh.numberOfCells(), Vector2{0.0, 0.0}),
                   ScalarField(cavity.mesh.numberOfCells(), 0.0));
  ASSERT_EQ(steady.status, SIMPLEStatus::Converged);

  const PISO piso(cavity.mesh, cavity.fluid, cavity.velocityBoundaries, cavity.pressureBoundaries,
                  makePisoSettings(), /*referenceCell=*/0);
  const TransientSolver solver(piso, /*cflFailAbove=*/1e6);
  const Real dt = 0.02;
  const Real finalTime = 15.0;  // longer than the recirculation-development test above
  const TransientResult transient =
      solver.solve(restState(cavity.mesh, cavity.fluid, cavity.velocityBoundaries),
                   TimeController(0.0, finalTime, dt, 1'000'000));
  ASSERT_EQ(transient.status, TransientStatus::Completed);

  // Velocity: cell-by-cell L2 relative to the steady field's own norm.
  Real diffSumSquares = 0.0;
  Real steadySumSquares = 0.0;
  for (Index i = 0; i < cavity.mesh.numberOfCells(); ++i) {
    const Real dux = transient.finalState.velocity[i].x - steady.velocity[i].x;
    const Real duy = transient.finalState.velocity[i].y - steady.velocity[i].y;
    diffSumSquares += dux * dux + duy * duy;
    steadySumSquares +=
        steady.velocity[i].x * steady.velocity[i].x + steady.velocity[i].y * steady.velocity[i].y;
  }
  const Real velocityRelativeL2 = std::sqrt(diffSumSquares / steadySumSquares);
  EXPECT_LT(velocityRelativeL2, 0.1)
      << "transient PISO's long-time velocity has not approached the steady SIMPLE solution";

  // Pressure: both solves share the same referenceCell=0 pin (p'=0
  // forced there every correction, and SIMPLE's own outer loop starts
  // from p=0 with the same pin), so comparing raw pressure -- not just
  // its gradient -- is meaningful here (no independent gauge choice to
  // reconcile, unlike the open-boundary Poiseuille case).
  Real pressureDiffSumSquares = 0.0;
  Real pressureSteadySumSquares = 0.0;
  for (Index i = 0; i < cavity.mesh.numberOfCells(); ++i) {
    const Real dp = transient.finalState.pressure[i] - steady.pressure[i];
    pressureDiffSumSquares += dp * dp;
    pressureSteadySumSquares += steady.pressure[i] * steady.pressure[i];
  }
  if (pressureSteadySumSquares > 1e-12) {
    EXPECT_LT(std::sqrt(pressureDiffSumSquares / pressureSteadySumSquares), 0.2);
  }

  // Authoritative flux / continuity / global mass imbalance.
  Real maxFluxDiff = 0.0;
  for (Index i = 0; i < cavity.mesh.numberOfFaces(); ++i) {
    maxFluxDiff =
        std::max(maxFluxDiff, std::abs(transient.finalState.massFlux[i] - steady.massFlux[i]));
  }
  EXPECT_LT(maxFluxDiff, 0.05) << "authoritative face flux has not approached the steady solution";
  EXPECT_LT(transient.history.back().massImbalance, 1e-6);
  EXPECT_LT(steady.globalMassImbalance, 1e-6);
}

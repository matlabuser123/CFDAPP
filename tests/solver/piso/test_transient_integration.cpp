#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/PISO.hpp"
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
using cfd::solver::TimeController;
using cfd::solver::TransientResult;
using cfd::solver::TransientSolver;
using cfd::solver::TransientState;
using cfd::solver::TransientStatus;
using cfd::solver::TransientStepResult;
using cfd::solver::TransientStepStatus;

namespace {

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh, Vector2 lidVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(lidVelocity));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

PISOSettings makeProbeSettings() {
  LinearSolverSettings linear;
  linear.maxIterations = 500;
  linear.absoluteTolerance = 1e-12;
  linear.relativeTolerance = 1e-10;
  PISOSettings settings;
  settings.momentumSolver = linear;
  settings.pressureSolver = linear;
  return settings;
}

TransientState makeRestState(const Mesh& mesh, const FluidProperties& fluid,
                             const BoundaryConditionSet& velocityBoundaries) {
  TransientState state;
  state.velocity = VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0});
  state.pressure = ScalarField(mesh.numberOfCells(), 0.0);
  state.massFlux = calculateMassFlux(mesh, state.velocity, fluid, velocityBoundaries);
  return state;
}

}  // namespace

// --- The strongest regression: a direct PISO call sequence == the same --
// --- sequence driven through TransientSolver -----------------------------

TEST(PisoTransientIntegrationTest, DirectPisoSequenceMatchesTransientSolverBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);
  const Index referenceCell = 0;
  const Real dt = 0.01;
  const Index stepCount = 3;

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(),
                  referenceCell);
  const TransientState initialState = makeRestState(mesh, fluid, velocityBoundaries);

  // Direct sequence: PISO owns one timestep's solve; this test owns
  // threading the state and calling it repeatedly -- no TransientSolver
  // involved at all. Drives dt from a *parallel* TimeController (rather
  // than a hardcoded literal) so the dt bit pattern genuinely matches
  // what TransientSolver's own internal loop uses -- TimeController's
  // deltaT() is computed as a subtraction of two independently-rounded
  // times (already validated, deliberate floating-point behavior --
  // TODO.md P2 -- TimeController notes), not simply the nominal deltaT
  // repeated verbatim, so a literal dt=0.01 used three times is *not*
  // guaranteed bit-identical to TimeController's own three deltaT()
  // calls even though both equal 0.01 to ~15 significant digits.
  TimeController referenceTimeController(0.0, 0.03, dt, 100);
  TransientState directState = initialState;
  std::vector<TransientStepResult> directResults;
  std::vector<Real> referenceDts;
  std::vector<Real> referenceTimes;
  for (Index i = 0; i < stepCount; ++i) {
    const Real stepDt = referenceTimeController.deltaT();
    const auto stepResult = piso.solveTimeStep(directState, stepDt);
    ASSERT_EQ(stepResult.status, TransientStepStatus::Converged) << "direct step " << i;
    directState = stepResult.state;
    directResults.push_back(stepResult);
    referenceDts.push_back(stepDt);
    referenceTimeController.advance();
    referenceTimes.push_back(referenceTimeController.time());
  }

  // Same PISO instance, same dt sequence (3 equal steps -- 0.03/0.01),
  // driven through TransientSolver instead. A generous cflFailAbove keeps
  // this purely about state/diagnostic threading, not CFL rejection.
  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);
  const TransientResult result =
      transientSolver.solve(initialState, TimeController(0.0, 0.03, dt, 100));

  ASSERT_EQ(result.status, TransientStatus::Completed);
  ASSERT_EQ(result.history.size(), static_cast<std::size_t>(stepCount));

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(result.finalState.pressure[i], directState.pressure[i]) << "cell " << i;
    EXPECT_EQ(result.finalState.velocity[i].x, directState.velocity[i].x) << "cell " << i;
    EXPECT_EQ(result.finalState.velocity[i].y, directState.velocity[i].y) << "cell " << i;
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_EQ(result.finalState.massFlux[i], directState.massFlux[i]) << "face " << i;
  }

  for (std::size_t i = 0; i < directResults.size(); ++i) {
    EXPECT_EQ(result.history[i].step, i + 1);
    // Compared against a second, independently-constructed (but
    // identically-parameterized) TimeController's own trajectory --
    // TimeController's determinism is already established elsewhere, so
    // this is a genuine bit-identical check, not a tolerance-loosened
    // approximation of one.
    EXPECT_EQ(result.history[i].time, referenceTimes[i]) << "step " << i;
    EXPECT_EQ(result.history[i].deltaT, referenceDts[i]) << "step " << i;
    EXPECT_EQ(result.history[i].maxCFL, directResults[i].maxCFL) << "step " << i;
    EXPECT_EQ(result.history[i].continuityResidual, directResults[i].continuityResidual)
        << "step " << i;
    EXPECT_EQ(result.history[i].massImbalance, directResults[i].massImbalance) << "step " << i;
  }
  // The final recorded time lands exactly on endTime, not merely close to
  // it (TimeController's own snap-to-endTime guarantee, still holding
  // with a real, non-stub stepper driving it).
  EXPECT_EQ(result.history.back().time, 0.03);
}

// --- Failure handling: PISO's own forced-failure technique (PISO-H), ----
// --- now observed through TransientSolver's acceptance policy -----------

TEST(PisoTransientIntegrationTest, FailedPisoStepIsRejectedAndLeavesAcceptedStateBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  // Mirrors PisoTest.ForcedMomentumFailurePropagatesAsMomentumFailure --
  // an intentionally impossible inner momentum solve.
  PISOSettings settings;
  settings.momentumSolver.maxIterations = 1;
  settings.momentumSolver.absoluteTolerance = 1e-14;
  settings.momentumSolver.relativeTolerance = 1e-14;
  settings.pressureSolver = makeProbeSettings().pressureSolver;

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, settings, 0);
  const TransientState initialState = makeRestState(mesh, fluid, velocityBoundaries);
  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);

  const TransientResult result =
      transientSolver.solve(initialState, TimeController(0.0, 1.0, 0.25, 100));

  EXPECT_EQ(result.status, TransientStatus::MomentumFailure);
  // No step was ever accepted -- the very first attempt already fails.
  EXPECT_TRUE(result.history.empty());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(result.finalState.pressure[i], initialState.pressure[i]);
    EXPECT_DOUBLE_EQ(result.finalState.velocity[i].x, initialState.velocity[i].x);
    EXPECT_DOUBLE_EQ(result.finalState.velocity[i].y, initialState.velocity[i].y);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_DOUBLE_EQ(result.finalState.massFlux[i], initialState.massFlux[i]);
  }
}

// --- TimeController behavior with a real PISO stepper --------------------

TEST(PisoTransientIntegrationTest, ShortenedFinalDtReachesEndTimeWithRealPiso) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const TransientState initialState = makeRestState(mesh, fluid, velocityBoundaries);
  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);

  // 0.01, 0.01, then a shortened 0.005 to land exactly on end=0.025.
  const TransientResult result =
      transientSolver.solve(initialState, TimeController(0.0, 0.025, 0.01, 100));

  ASSERT_EQ(result.status, TransientStatus::Completed);
  ASSERT_EQ(result.history.size(), 3u);
  EXPECT_NEAR(result.history[0].deltaT, 0.01, 1e-12);
  EXPECT_NEAR(result.history[1].deltaT, 0.01, 1e-12);
  EXPECT_NEAR(result.history[2].deltaT, 0.005, 1e-12);
  EXPECT_EQ(result.history.back().time, 0.025);
}

TEST(PisoTransientIntegrationTest, MaxStepsTerminationWithRealPiso) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const TransientState initialState = makeRestState(mesh, fluid, velocityBoundaries);
  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);

  // Needs 4 steps at deltaT=0.01 to reach end=0.04; only 2 allowed.
  const TransientResult result =
      transientSolver.solve(initialState, TimeController(0.0, 0.04, 0.01, 2));

  EXPECT_EQ(result.status, TransientStatus::MaxTimeSteps);
  EXPECT_EQ(result.history.size(), 2u);
  EXPECT_NEAR(result.history.back().time, 0.02, 1e-12);
}

// --- CFL: computed from the accepted, corrected F2; diagnostic only -----

TEST(PisoTransientIntegrationTest, CflRemainsDiagnosticOnlyUnderGenerousCflFailAbove) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  // A deliberately large dt (well beyond what a typical explicit-scheme
  // CFL limit would allow) -- implicit Euler's momentum predictor and the
  // pressure-correction solves are unconditionally stable, so PISO must
  // still converge every step; only whether the *run* accepts a high-CFL
  // step is at TransientSolver's discretion via cflFailAbove.
  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const TransientState initialState = makeRestState(mesh, fluid, velocityBoundaries);
  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);

  const TransientResult result =
      transientSolver.solve(initialState, TimeController(0.0, 1.0, 0.5, 100));

  ASSERT_EQ(result.status, TransientStatus::Completed);
  for (const auto& record : result.history) {
    EXPECT_TRUE(std::isfinite(record.maxCFL));
    EXPECT_GE(record.maxCFL, 0.0);
  }
}

TEST(PisoTransientIntegrationTest, RealComputedCflCanTriggerCflViolationWhenFailAboveIsLow) {
  // The mirror image of the above: proves the CFL value flowing into
  // TransientSolver is genuinely computed from real physics (not some
  // stub constant) -- setting cflFailAbove below what a real predictor
  // actually produces genuinely rejects the step. PISO itself never
  // decides this; TransientSolver does (TODO.md P2 -- PISO-I notes: "a
  // high CFL should not silently change dt in PISO-I").
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const TransientState initialState = makeRestState(mesh, fluid, velocityBoundaries);

  // First, discover the actual pre-step CFL PISO reports for this exact
  // configuration (from-rest, so it is 0 at step 1) after one accepted
  // step, then use a large-enough dt that the *second* step's pre-step
  // CFL (computed from the now-moving accepted flux) is large.
  const TransientSolver generousSolver(piso, /*cflFailAbove=*/1000.0);
  const TransientResult generousResult =
      generousSolver.solve(initialState, TimeController(0.0, 1.0, 0.5, 2));
  ASSERT_EQ(generousResult.status, TransientStatus::Completed);
  ASSERT_EQ(generousResult.history.size(), 2u);
  const Real secondStepCFL = generousResult.history[1].maxCFL;
  ASSERT_GT(secondStepCFL, 0.0);  // genuinely nonzero once the flow has developed

  const TransientSolver strictSolver(piso, /*cflFailAbove=*/secondStepCFL / 2.0);
  const TransientResult strictResult =
      strictSolver.solve(initialState, TimeController(0.0, 1.0, 0.5, 2));

  EXPECT_EQ(strictResult.status, TransientStatus::CFLViolation);
  // The first step (whose own CFL is below the strict threshold) was
  // still accepted before the second step's higher CFL rejected the run.
  EXPECT_EQ(strictResult.history.size(), 1u);
}

// --- Determinism -----------------------------------------------------------

TEST(PisoTransientIntegrationTest, RepeatedIdenticalMultiStepRunIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const TransientState initialState = makeRestState(mesh, fluid, velocityBoundaries);
  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);

  const TransientResult resultA =
      transientSolver.solve(initialState, TimeController(0.0, 0.03, 0.01, 100));
  const TransientResult resultB =
      transientSolver.solve(initialState, TimeController(0.0, 0.03, 0.01, 100));

  ASSERT_EQ(resultA.status, TransientStatus::Completed);
  ASSERT_EQ(resultB.status, TransientStatus::Completed);
  ASSERT_EQ(resultA.history.size(), resultB.history.size());
  for (std::size_t i = 0; i < resultA.history.size(); ++i) {
    EXPECT_EQ(resultA.history[i].time, resultB.history[i].time);
    EXPECT_EQ(resultA.history[i].deltaT, resultB.history[i].deltaT);
    EXPECT_EQ(resultA.history[i].maxCFL, resultB.history[i].maxCFL);
    EXPECT_EQ(resultA.history[i].continuityResidual, resultB.history[i].continuityResidual);
    EXPECT_EQ(resultA.history[i].massImbalance, resultB.history[i].massImbalance);
  }
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(resultA.finalState.pressure[i], resultB.finalState.pressure[i]);
    EXPECT_EQ(resultA.finalState.velocity[i].x, resultB.finalState.velocity[i].x);
    EXPECT_EQ(resultA.finalState.velocity[i].y, resultB.finalState.velocity[i].y);
  }
}

// --- Caller-side immutability ------------------------------------------

TEST(PisoTransientIntegrationTest, CallerInitialStateArgumentIsNotMutated) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const PISO piso(mesh, fluid, velocityBoundaries, pressureBoundaries, makeProbeSettings(), 0);
  const TransientState initialState = makeRestState(mesh, fluid, velocityBoundaries);
  const VectorField velocityCopy = initialState.velocity;
  const ScalarField pressureCopy = initialState.pressure;
  const SurfaceField massFluxCopy = initialState.massFlux;

  const TransientSolver transientSolver(piso, /*cflFailAbove=*/1000.0);
  const TransientResult result =
      transientSolver.solve(initialState, TimeController(0.0, 0.02, 0.01, 100));
  ASSERT_EQ(result.status, TransientStatus::Completed);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_DOUBLE_EQ(initialState.velocity[i].x, velocityCopy[i].x);
    EXPECT_DOUBLE_EQ(initialState.velocity[i].y, velocityCopy[i].y);
    EXPECT_DOUBLE_EQ(initialState.pressure[i], pressureCopy[i]);
  }
  for (Index i = 0; i < mesh.numberOfFaces(); ++i) {
    EXPECT_DOUBLE_EQ(initialState.massFlux[i], massFluxCopy[i]);
  }
}

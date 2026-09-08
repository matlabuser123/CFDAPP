#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/solver/TimeController.hpp"
#include "cfd/solver/TransientSolver.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::solver::TimeController;
using cfd::solver::TransientResult;
using cfd::solver::TransientSolver;
using cfd::solver::TransientState;
using cfd::solver::TransientStatus;
using cfd::solver::TransientStepResult;
using cfd::solver::TransientStepSolver;
using cfd::solver::TransientStepStatus;

namespace {

constexpr Real kNaN = std::numeric_limits<Real>::quiet_NaN();

// A test-only, mesh/physics-free stepper: on each call, doubles pressure
// and advances velocity.x by dt (an arbitrary but easily-checked rule --
// N successful calls from pressure=1 give pressure=2^N, letting tests
// confirm the *previous accepted state*, not some stale or garbage one,
// is really what each call receives). Every test configures exactly what
// (if anything) should go wrong and when, without any real physics or
// PISO existing yet.
class StubStepSolver : public TransientStepSolver {
 public:
  StubStepSolver(Index cellCount, Index faceCount) : cellCount_(cellCount), faceCount_(faceCount) {}

  // 1-indexed: the call() at which to report failStatus_ instead of
  // succeeding normally. 0 (default) means never.
  void failOnCall(Index callNumber, TransientStepStatus status) {
    failOnCall_ = callNumber;
    failStatus_ = status;
  }
  // Report Converged but with a non-finite value planted in the state,
  // to test TransientSolver's own independent finite check (not just
  // trusting a stepper that claims success).
  void injectNonFiniteOnCall(Index callNumber) { injectNonFiniteOnCall_ = callNumber; }
  void setMaxCFL(Real value) { maxCFL_ = value; }

  [[nodiscard]] Index callCount() const { return callCount_; }
  [[nodiscard]] const std::vector<Real>& capturedDts() const { return capturedDts_; }
  [[nodiscard]] const std::vector<Real>& capturedPreviousPressures() const {
    return capturedPreviousPressures_;
  }

  [[nodiscard]] TransientStepResult solveTimeStep(const TransientState& previousState,
                                                  Real dt) const override {
    ++callCount_;
    capturedDts_.push_back(dt);
    capturedPreviousPressures_.push_back(previousState.pressure[0]);

    if (failOnCall_ != 0 && callCount_ == failOnCall_) {
      return TransientStepResult{TransientState{}, failStatus_, 0.0, 0.0, 0.0};
    }

    TransientState newState;
    newState.velocity = VectorField(cellCount_);
    newState.pressure = ScalarField(cellCount_);
    newState.massFlux = SurfaceField(faceCount_);
    for (Index i = 0; i < cellCount_; ++i) {
      newState.velocity[i] = previousState.velocity[i] + Vector2{dt, 0.0};
      newState.pressure[i] = previousState.pressure[i] * 2.0;
    }
    for (Index i = 0; i < faceCount_; ++i) {
      newState.massFlux[i] = previousState.massFlux[i];
    }
    if (injectNonFiniteOnCall_ != 0 && callCount_ == injectNonFiniteOnCall_) {
      newState.pressure[0] = kNaN;
    }

    return TransientStepResult{std::move(newState), TransientStepStatus::Converged, maxCFL_, 1e-8,
                               1e-9};
  }

 private:
  Index cellCount_;
  Index faceCount_;
  Index failOnCall_ = 0;
  TransientStepStatus failStatus_ = TransientStepStatus::MomentumFailure;
  Index injectNonFiniteOnCall_ = 0;
  Real maxCFL_ = 0.1;

  mutable Index callCount_ = 0;
  mutable std::vector<Real> capturedDts_;
  mutable std::vector<Real> capturedPreviousPressures_;
};

TransientState makeInitialState(Real pressure) {
  TransientState state;
  state.velocity = VectorField(1, Vector2{0.0, 0.0});
  state.pressure = ScalarField(1, pressure);
  state.massFlux = SurfaceField(1, 0.0);
  return state;
}

}  // namespace

TEST(TransientSolverTest, CompletesAndThreadsStateThroughEveryStep) {
  StubStepSolver stub(1, 1);
  const TransientSolver solver(stub, /*cflFailAbove=*/1.0);

  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.25, 100));

  EXPECT_EQ(result.status, TransientStatus::Completed);
  ASSERT_EQ(result.history.size(), 4u);
  EXPECT_DOUBLE_EQ(result.finalState.pressure[0], 16.0);   // 1 -> 2 -> 4 -> 8 -> 16
  EXPECT_DOUBLE_EQ(result.finalState.velocity[0].x, 1.0);  // 4 * 0.25
  EXPECT_EQ(result.history.back().step, 4u);
  EXPECT_DOUBLE_EQ(result.history.back().time, 1.0);
  // Every call actually received the *previously accepted* pressure, not
  // a stale or repeated one.
  const std::vector<Real> expectedPreviousPressures = {1.0, 2.0, 4.0, 8.0};
  EXPECT_EQ(stub.capturedPreviousPressures(), expectedPreviousPressures);
}

TEST(TransientSolverTest, DtSequenceMatchesTimeControllerIncludingShortenedFinalStep) {
  StubStepSolver stub(1, 1);
  const TransientSolver solver(stub, 1.0);

  // TODO.md P2 section 4's worked example: 0.3, 0.3, 0.3, then a
  // shortened 0.1 to land on end=1.0.
  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.3, 100));

  EXPECT_EQ(result.status, TransientStatus::Completed);
  const std::vector<Real> expectedDts = {0.3, 0.3, 0.3, 0.1};
  ASSERT_EQ(stub.capturedDts().size(), expectedDts.size());
  for (std::size_t i = 0; i < expectedDts.size(); ++i) {
    EXPECT_NEAR(stub.capturedDts()[i], expectedDts[i], 1e-12) << "at call " << i;
  }
}

TEST(TransientSolverTest, MaxTimeStepsStopsBeforeReachingEndTime) {
  StubStepSolver stub(1, 1);
  const TransientSolver solver(stub, 1.0);

  // Needs 4 steps at deltaT=0.25 to reach end=1.0; only 2 allowed.
  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.25, 2));

  EXPECT_EQ(result.status, TransientStatus::MaxTimeSteps);
  EXPECT_EQ(result.history.size(), 2u);
  EXPECT_DOUBLE_EQ(result.finalState.pressure[0], 4.0);  // 1 -> 2 -> 4, then stopped
}

TEST(TransientSolverTest, FailedStepIsNotAcceptedAndStopsTheRun) {
  StubStepSolver stub(1, 1);
  stub.failOnCall(2, TransientStepStatus::MomentumFailure);
  const TransientSolver solver(stub, 1.0);

  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.25, 100));

  EXPECT_EQ(result.status, TransientStatus::MomentumFailure);
  // Only the one successful step (call 1) before the failure (call 2) was
  // accepted.
  EXPECT_EQ(result.history.size(), 1u);
  EXPECT_DOUBLE_EQ(result.finalState.pressure[0], 2.0);  // the failed call's own state discarded
}

TEST(TransientSolverTest, PressureCorrectionFailurePropagates) {
  StubStepSolver stub(1, 1);
  stub.failOnCall(1, TransientStepStatus::PressureCorrectionFailure);
  const TransientSolver solver(stub, 1.0);

  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.25, 100));

  EXPECT_EQ(result.status, TransientStatus::PressureCorrectionFailure);
  EXPECT_TRUE(result.history.empty());
  EXPECT_DOUBLE_EQ(result.finalState.pressure[0], 1.0);  // still the untouched initial state
}

TEST(TransientSolverTest, StepReportedNonFiniteStatePropagates) {
  StubStepSolver stub(1, 1);
  stub.failOnCall(1, TransientStepStatus::NonFiniteState);
  const TransientSolver solver(stub, 1.0);

  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.25, 100));

  EXPECT_EQ(result.status, TransientStatus::NonFiniteState);
  EXPECT_TRUE(result.history.empty());
}

TEST(TransientSolverTest, InvalidConfigurationFromStepperPropagates) {
  StubStepSolver stub(1, 1);
  stub.failOnCall(1, TransientStepStatus::InvalidConfiguration);
  const TransientSolver solver(stub, 1.0);

  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.25, 100));

  EXPECT_EQ(result.status, TransientStatus::InvalidConfiguration);
}

// The stepper itself claims Converged, but the state it returns contains
// a non-finite value -- TransientSolver must catch this independently,
// not just trust the reported status (TODO.md P2 section 15/51).
TEST(TransientSolverTest, CatchesNonFiniteStateEvenWhenStepperClaimsConverged) {
  StubStepSolver stub(1, 1);
  stub.injectNonFiniteOnCall(2);
  const TransientSolver solver(stub, 1.0);

  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.25, 100));

  EXPECT_EQ(result.status, TransientStatus::NonFiniteState);
  // Call 1 succeeded and was accepted; call 2's non-finite result was not.
  EXPECT_EQ(result.history.size(), 1u);
  EXPECT_DOUBLE_EQ(result.finalState.pressure[0], 2.0);
}

TEST(TransientSolverTest, StepExceedingCflFailAboveIsRejected) {
  StubStepSolver stub(1, 1);
  stub.setMaxCFL(5.0);
  const TransientSolver solver(stub, /*cflFailAbove=*/1.0);

  const TransientResult result =
      solver.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.25, 100));

  EXPECT_EQ(result.status, TransientStatus::CFLViolation);
  EXPECT_TRUE(result.history.empty());
  EXPECT_DOUBLE_EQ(result.finalState.pressure[0], 1.0);
}

TEST(TransientSolverTest, RejectsNonPositiveOrNonFiniteCflFailAbove) {
  StubStepSolver stub(1, 1);
  EXPECT_THROW((TransientSolver(stub, 0.0)), cfd::InvalidArgumentError);
  EXPECT_THROW((TransientSolver(stub, -1.0)), cfd::InvalidArgumentError);
  EXPECT_THROW((TransientSolver(stub, kNaN)), cfd::InvalidArgumentError);
  EXPECT_THROW((TransientSolver(stub, std::numeric_limits<Real>::infinity())),
               cfd::InvalidArgumentError);
}

TEST(TransientSolverTest, RepeatedRunsAreDeterministic) {
  StubStepSolver stubA(1, 1);
  StubStepSolver stubB(1, 1);
  const TransientSolver solverA(stubA, 1.0);
  const TransientSolver solverB(stubB, 1.0);

  const TransientResult resultA =
      solverA.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.3, 100));
  const TransientResult resultB =
      solverB.solve(makeInitialState(1.0), TimeController(0.0, 1.0, 0.3, 100));

  EXPECT_EQ(resultA.status, resultB.status);
  ASSERT_EQ(resultA.history.size(), resultB.history.size());
  for (std::size_t i = 0; i < resultA.history.size(); ++i) {
    EXPECT_EQ(resultA.history[i].time, resultB.history[i].time);
    EXPECT_EQ(resultA.history[i].deltaT, resultB.history[i].deltaT);
  }
  EXPECT_DOUBLE_EQ(resultA.finalState.pressure[0], resultB.finalState.pressure[0]);
}

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/solver/TimeController.hpp"

using cfd::Index;
using cfd::Real;
using cfd::solver::TimeController;

namespace {
constexpr Real kNaN = std::numeric_limits<Real>::quiet_NaN();
constexpr Real kInf = std::numeric_limits<Real>::infinity();
}  // namespace

TEST(TimeControllerTest, InitialStateIsStartTimeAndStepZero) {
  const TimeController tc(0.0, 1.0, 0.3, 100);
  EXPECT_DOUBLE_EQ(tc.time(), 0.0);
  EXPECT_EQ(tc.step(), 0u);
  EXPECT_FALSE(tc.finished());
}

TEST(TimeControllerTest, NormalSteppingUsesNominalDeltaT) {
  TimeController tc(0.0, 10.0, 1.0, 100);
  for (int i = 0; i < 5; ++i) {
    EXPECT_DOUBLE_EQ(tc.deltaT(), 1.0);
    tc.advance();
  }
  EXPECT_DOUBLE_EQ(tc.time(), 5.0);
  EXPECT_EQ(tc.step(), 5u);
  EXPECT_FALSE(tc.finished());
}

TEST(TimeControllerTest, StepCounterIncrementsOncePerAdvance) {
  TimeController tc(0.0, 100.0, 1.0, 100);
  EXPECT_EQ(tc.step(), 0u);
  tc.advance();
  EXPECT_EQ(tc.step(), 1u);
  tc.advance();
  EXPECT_EQ(tc.step(), 2u);
}

// TODO.md P2 section 4's worked example: start=0.9, deltaT=0.2, end=1.0 ->
// final deltaT should be shortened to 0.1, landing exactly on end=1.0.
TEST(TimeControllerTest, FinalStepIsShortenedToLandExactlyOnEndTime) {
  TimeController tc(0.9, 1.0, 0.2, 100);
  EXPECT_DOUBLE_EQ(tc.deltaT(), 0.1);
  tc.advance();
  EXPECT_DOUBLE_EQ(tc.time(), 1.0);
  EXPECT_TRUE(tc.finished());
}

// The user-facing example: start=0, end=1, deltaT=0.3 -> 0.0, 0.3, 0.6,
// 0.9, 1.0 (not overshooting to 1.2).
TEST(TimeControllerTest, SequenceMatchesWorkedExample) {
  TimeController tc(0.0, 1.0, 0.3, 100);
  const std::vector<Real> expected = {0.0, 0.3, 0.6, 0.9, 1.0};
  std::vector<Real> actual = {tc.time()};
  while (!tc.finished()) {
    tc.advance();
    actual.push_back(tc.time());
  }
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_NEAR(actual[i], expected[i], 1e-12) << "at index " << i;
  }
  EXPECT_DOUBLE_EQ(actual.back(), 1.0);  // exact, not merely close
}

// A deltaT that does not evenly divide (end-start) in binary floating
// point (0.1 is not exactly representable) must not need an extra,
// vanishingly-short step to cross endTime_ -- see TimeController.cpp's
// timeAtStep for why a tolerance is needed at all.
TEST(TimeControllerTest, InexactDeltaTDoesNotProduceAnExtraTinyStep) {
  TimeController tc(0.0, 1.0, 0.1, 100);
  Index steps = 0;
  while (!tc.finished()) {
    tc.advance();
    ++steps;
  }
  EXPECT_EQ(steps, 10u);
  EXPECT_DOUBLE_EQ(tc.time(), 1.0);
}

TEST(TimeControllerTest, SingleStepCaseReachesEndExactly) {
  TimeController tc(0.0, 1.0, 1.0, 1);
  EXPECT_DOUBLE_EQ(tc.deltaT(), 1.0);
  tc.advance();
  EXPECT_DOUBLE_EQ(tc.time(), 1.0);
  EXPECT_EQ(tc.step(), 1u);
  EXPECT_TRUE(tc.finished());
  EXPECT_TRUE(tc.reachedEndTime());
}

TEST(TimeControllerTest, StartEqualsEndIsImmediatelyFinished) {
  const TimeController tc(5.0, 5.0, 0.1, 10);
  EXPECT_DOUBLE_EQ(tc.time(), 5.0);
  EXPECT_TRUE(tc.finished());
  EXPECT_TRUE(tc.reachedEndTime());
  EXPECT_DOUBLE_EQ(tc.deltaT(), 0.0);
}

TEST(TimeControllerTest, MaximumStepTerminationStopsBeforeEndTime) {
  TimeController tc(0.0, 100.0, 1.0, 5);
  for (Index i = 0; i < 5; ++i) {
    ASSERT_FALSE(tc.finished());
    tc.advance();
  }
  EXPECT_TRUE(tc.finished());
  EXPECT_EQ(tc.step(), 5u);
  EXPECT_DOUBLE_EQ(tc.time(), 5.0);  // far short of endTime=100
  EXPECT_DOUBLE_EQ(tc.deltaT(), 0.0);
  EXPECT_FALSE(tc.reachedEndTime());  // finished() only because maxSteps was hit
}

TEST(TimeControllerTest, ReachedEndTimeDistinguishesCompletionFromMaxSteps) {
  TimeController ranToEnd(0.0, 1.0, 0.25, 100);
  while (!ranToEnd.finished()) {
    ranToEnd.advance();
  }
  EXPECT_TRUE(ranToEnd.finished());
  EXPECT_TRUE(ranToEnd.reachedEndTime());

  TimeController hitMaxSteps(0.0, 1.0, 0.25, 2);  // needs 4 steps to reach end, only 2 allowed
  while (!hitMaxSteps.finished()) {
    hitMaxSteps.advance();
  }
  EXPECT_TRUE(hitMaxSteps.finished());
  EXPECT_FALSE(hitMaxSteps.reachedEndTime());
}

TEST(TimeControllerTest, AdvanceAfterFinishedThrows) {
  TimeController tc(0.0, 1.0, 1.0, 1);
  tc.advance();
  ASSERT_TRUE(tc.finished());
  EXPECT_THROW(tc.advance(), cfd::InvalidArgumentError);
}

TEST(TimeControllerTest, RejectsNonPositiveDeltaT) {
  EXPECT_THROW(TimeController(0.0, 1.0, 0.0, 10), cfd::InvalidArgumentError);
  EXPECT_THROW(TimeController(0.0, 1.0, -0.1, 10), cfd::InvalidArgumentError);
}

TEST(TimeControllerTest, RejectsEndTimeBeforeStartTime) {
  EXPECT_THROW(TimeController(1.0, 0.0, 0.1, 10), cfd::InvalidArgumentError);
}

TEST(TimeControllerTest, RejectsZeroMaxSteps) {
  EXPECT_THROW(TimeController(0.0, 1.0, 0.1, 0), cfd::InvalidArgumentError);
}

TEST(TimeControllerTest, RejectsNonFiniteInputs) {
  EXPECT_THROW(TimeController(kNaN, 1.0, 0.1, 10), cfd::InvalidArgumentError);
  EXPECT_THROW(TimeController(kInf, 1.0, 0.1, 10), cfd::InvalidArgumentError);
  EXPECT_THROW(TimeController(0.0, kNaN, 0.1, 10), cfd::InvalidArgumentError);
  EXPECT_THROW(TimeController(0.0, kInf, 0.1, 10), cfd::InvalidArgumentError);
  EXPECT_THROW(TimeController(0.0, 1.0, kNaN, 10), cfd::InvalidArgumentError);
  EXPECT_THROW(TimeController(0.0, 1.0, kInf, 10), cfd::InvalidArgumentError);
}

// Repeated, independently-constructed controllers stepped the same way
// must produce bit-identical time sequences (TODO.md P2 section 5:
// "deterministic stepping") -- expected structurally (timeAtStep is a
// pure function of the step index, no accumulated state), verified here
// rather than only asserted.
TEST(TimeControllerTest, RepeatedRunsAreBitIdentical) {
  TimeController a(0.0, 1.0, 0.13, 1000);
  TimeController b(0.0, 1.0, 0.13, 1000);
  while (!a.finished()) {
    ASSERT_FALSE(b.finished());
    ASSERT_EQ(a.step(), b.step());
    ASSERT_EQ(a.time(), b.time());  // bit-identical, not just EXPECT_NEAR
    ASSERT_EQ(a.deltaT(), b.deltaT());
    a.advance();
    b.advance();
  }
  EXPECT_TRUE(b.finished());
  EXPECT_EQ(a.time(), b.time());
  EXPECT_EQ(a.step(), b.step());
}

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "cfd/core/Timer.hpp"

TEST(CoreTimer, ElapsedTimeIsNonNegative) {
  const cfd::Timer timer;
  EXPECT_GE(timer.elapsedSeconds(), 0.0);
  EXPECT_GE(timer.elapsedMilliseconds(), 0.0);
}

TEST(CoreTimer, ElapsedTimeIncreases) {
  cfd::Timer timer;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const double firstElapsed = timer.elapsedSeconds();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const double secondElapsed = timer.elapsedSeconds();

  EXPECT_GT(secondElapsed, firstElapsed);
}

TEST(CoreTimer, ResetRestartsTiming) {
  cfd::Timer timer;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const double beforeReset = timer.elapsedSeconds();

  timer.reset();
  const double afterReset = timer.elapsedSeconds();

  EXPECT_LT(afterReset, beforeReset);
}

TEST(CoreTimer, SecondsAndMillisecondsAreConsistent) {
  cfd::Timer timer;
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  const double seconds = timer.elapsedSeconds();
  const double milliseconds = timer.elapsedMilliseconds();

  EXPECT_NEAR(milliseconds, seconds * 1000.0, 5.0);  // generous, not brittle
}

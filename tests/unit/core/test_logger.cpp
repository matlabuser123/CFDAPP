#include <gtest/gtest.h>

#include <functional>
#include <iostream>
#include <sstream>
#include <string>

#include "cfd/core/Logger.hpp"

namespace {

std::string captureStdout(const std::function<void()>& action) {
  std::ostringstream captured;
  std::streambuf* const originalBuf = std::cout.rdbuf(captured.rdbuf());
  action();
  std::cout.rdbuf(originalBuf);
  return captured.str();
}

std::string captureStderr(const std::function<void()>& action) {
  std::ostringstream captured;
  std::streambuf* const originalBuf = std::cerr.rdbuf(captured.rdbuf());
  action();
  std::cerr.rdbuf(originalBuf);
  return captured.str();
}

class LoggerTest : public ::testing::Test {
 protected:
  void SetUp() override { originalLevel_ = cfd::Logger::instance().level(); }
  void TearDown() override { cfd::Logger::instance().setLevel(originalLevel_); }

  cfd::LogLevel originalLevel_ = cfd::LogLevel::Info;
};

}  // namespace

TEST_F(LoggerTest, DefaultableLevelRoundTrips) {
  cfd::Logger::instance().setLevel(cfd::LogLevel::Info);
  EXPECT_EQ(cfd::Logger::instance().level(), cfd::LogLevel::Info);
}

TEST_F(LoggerTest, LevelCanBeChanged) {
  cfd::Logger::instance().setLevel(cfd::LogLevel::Warning);
  EXPECT_EQ(cfd::Logger::instance().level(), cfd::LogLevel::Warning);
}

TEST_F(LoggerTest, MessagesAtOrAboveLevelAreEmittedWithPrefix) {
  cfd::Logger::instance().setLevel(cfd::LogLevel::Info);
  const std::string output = captureStdout([] { cfd::Logger::instance().info("hello"); });
  EXPECT_NE(output.find("[INFO]"), std::string::npos);
  EXPECT_NE(output.find("hello"), std::string::npos);
}

TEST_F(LoggerTest, MessagesBelowLevelAreFiltered) {
  cfd::Logger::instance().setLevel(cfd::LogLevel::Info);
  const std::string output =
      captureStdout([] { cfd::Logger::instance().debug("should be suppressed"); });
  EXPECT_TRUE(output.empty());
}

TEST_F(LoggerTest, WarningAndErrorGoToStderr) {
  cfd::Logger::instance().setLevel(cfd::LogLevel::Trace);

  const std::string warnOutput = captureStderr([] { cfd::Logger::instance().warning("careful"); });
  EXPECT_NE(warnOutput.find("[WARNING]"), std::string::npos);

  const std::string errorOutput = captureStderr([] { cfd::Logger::instance().error("failed"); });
  EXPECT_NE(errorOutput.find("[ERROR]"), std::string::npos);
}

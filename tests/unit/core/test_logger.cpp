#include <gtest/gtest.h>

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/core/Logger.hpp"

namespace {

std::string captureStderr(const std::function<void()>& action) {
  std::ostringstream captured;
  std::streambuf* const originalBuf = std::cerr.rdbuf(captured.rdbuf());
  action();
  std::cerr.rdbuf(originalBuf);
  return captured.str();
}

// Both at once. The P13-005 invariant is about which of the two streams a
// message lands on, and a capture of one stream alone cannot express that:
// "stderr contains it" and "stdout does not" are two different assertions.
struct CapturedStreams {
  std::string out;
  std::string err;
};

CapturedStreams captureBoth(const std::function<void()>& action) {
  std::ostringstream capturedOut;
  std::ostringstream capturedErr;
  std::streambuf* const originalOut = std::cout.rdbuf(capturedOut.rdbuf());
  std::streambuf* const originalErr = std::cerr.rdbuf(capturedErr.rdbuf());
  action();
  std::cout.rdbuf(originalOut);
  std::cerr.rdbuf(originalErr);
  return CapturedStreams{capturedOut.str(), capturedErr.str()};
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
  const std::string output = captureStderr([] { cfd::Logger::instance().info("hello"); });
  EXPECT_NE(output.find("[INFO]"), std::string::npos);
  EXPECT_NE(output.find("hello"), std::string::npos);
}

TEST_F(LoggerTest, MessagesBelowLevelAreFiltered) {
  cfd::Logger::instance().setLevel(cfd::LogLevel::Info);
  const CapturedStreams captured =
      captureBoth([] { cfd::Logger::instance().debug("should be suppressed"); });
  EXPECT_TRUE(captured.err.empty());
  EXPECT_TRUE(captured.out.empty());
}

// P13-005 (criterion C1). stdout belongs to the program's results -- the CLI's
// report is parsed by scripts and asserted on by CFDAppCliValidCase -- so a
// diagnostic landing there corrupts it. EVERY level goes to stderr, and this is
// the unit-level statement of that; CFDAppCliLogLinesNeverReachStdout is the
// same invariant measured on a real process.
TEST_F(LoggerTest, EveryLevelGoesToStderrAndNeverToStdout) {
  cfd::Logger::instance().setLevel(cfd::LogLevel::Trace);

  const std::vector<std::pair<std::function<void(std::string_view)>, std::string>> emitters{
      {[](std::string_view m) { cfd::Logger::instance().trace(m); }, "[TRACE]"},
      {[](std::string_view m) { cfd::Logger::instance().debug(m); }, "[DEBUG]"},
      {[](std::string_view m) { cfd::Logger::instance().info(m); }, "[INFO]"},
      {[](std::string_view m) { cfd::Logger::instance().warning(m); }, "[WARNING]"},
      {[](std::string_view m) { cfd::Logger::instance().error(m); }, "[ERROR]"},
  };

  for (const auto& [emit, tag] : emitters) {
    const CapturedStreams captured = captureBoth([&emit] { emit("channel check"); });
    EXPECT_NE(captured.err.find(tag), std::string::npos) << tag << " did not reach stderr";
    EXPECT_TRUE(captured.out.empty()) << tag << " leaked onto stdout: " << captured.out;
  }
}

// P13-005 (criterion V1/V2). One vocabulary, parsed in one place, so the CLI,
// the GUI and any future client cannot drift apart on what a level is called.
TEST(LogLevelNames, EveryLevelRoundTripsThroughItsName) {
  const std::vector<cfd::LogLevel> levels{cfd::LogLevel::Trace, cfd::LogLevel::Debug,
                                          cfd::LogLevel::Info, cfd::LogLevel::Warning,
                                          cfd::LogLevel::Error};
  for (const cfd::LogLevel level : levels) {
    const std::string_view name = cfd::logLevelName(level);
    EXPECT_FALSE(name.empty());
    const std::optional<cfd::LogLevel> parsed = cfd::parseLogLevel(name);
    ASSERT_TRUE(parsed.has_value())
        << "logLevelName produced a name parseLogLevel rejects: " << name;
    EXPECT_EQ(*parsed, level);
  }
}

TEST(LogLevelNames, UnknownNameIsRejectedRatherThanDefaulted) {
  // Silently defaulting would be the failure worth guarding: a run told to be
  // verbose that quietly was not sends a user hunting for diagnostics that
  // were never going to appear.
  EXPECT_FALSE(cfd::parseLogLevel("shout").has_value());
  EXPECT_FALSE(cfd::parseLogLevel("").has_value());
  EXPECT_FALSE(cfd::parseLogLevel("INFORMATIONAL").has_value());
}

TEST(LogLevelNames, TheAdvertisedNamesAreExactlyTheOnesThatParse) {
  // logLevelNames() is what the CLI prints when it rejects a level. If it
  // listed a name parseLogLevel does not accept, the error message would be
  // telling users to type something that will be rejected again.
  const std::string advertised(cfd::logLevelNames());
  ASSERT_FALSE(advertised.empty());

  std::size_t counted = 0;
  std::size_t begin = 0;
  while (begin <= advertised.size()) {
    std::size_t end = advertised.find(',', begin);
    if (end == std::string::npos) end = advertised.size();
    std::string name = advertised.substr(begin, end - begin);
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (!name.empty()) {
      EXPECT_TRUE(cfd::parseLogLevel(name).has_value())
          << "logLevelNames() advertises \"" << name << "\", which parseLogLevel rejects";
      ++counted;
    }
    if (end == advertised.size()) break;
    begin = end + 1;
  }
  EXPECT_EQ(counted, 5u) << "advertised: " << advertised;
}

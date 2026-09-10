// P5 -- Application, section 9 ("CLI/GUI execution equivalence"):
// ProjectRunner is the one production solver backend apps/cli/main.cpp
// and apps/gui's SimulationController both call -- exercised directly
// here (not through either front end) so this suite covers both by
// construction. gtest_discover_tests sets WORKING_DIRECTORY to
// CFDApp_SOURCE_DIR (same convention as tests/integration/case), so the
// relative fixture path below resolves against the repository root.
#include <gtest/gtest.h>

#include "cfd/app/ProjectRunner.hpp"

using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;

TEST(ProjectRunnerTest, ValidCaseConvergesAndExportsResults) {
  const ProjectRunResult run = ProjectRunner::run("tests/data/cases/valid_cavity");

  EXPECT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.caseDefinition.has_value());
  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_TRUE(run.simpleResult->converged());
  ASSERT_TRUE(run.exportSummary.has_value());
  EXPECT_TRUE(std::filesystem::exists(run.exportSummary->metadataPath));
  EXPECT_TRUE(std::filesystem::exists(run.exportSummary->residualsCsvPath));
  EXPECT_EQ(cfd::app::exitCodeFor(run.status), 0);
}

TEST(ProjectRunnerTest, MissingCaseDirectoryReportsInvalidCase) {
  const ProjectRunResult run = ProjectRunner::run("this/directory/does/not/exist");

  EXPECT_EQ(run.status, ProjectRunStatus::InvalidCase);
  EXPECT_FALSE(run.caseDefinition.has_value());
  EXPECT_FALSE(run.errorMessage.empty());
  EXPECT_EQ(cfd::app::exitCodeFor(run.status), 2);
}

TEST(ProjectRunnerTest, RepeatedRunsAreDeterministic) {
  const ProjectRunResult first = ProjectRunner::run("tests/data/cases/valid_cavity");
  const ProjectRunResult second = ProjectRunner::run("tests/data/cases/valid_cavity");

  ASSERT_TRUE(first.simpleResult.has_value());
  ASSERT_TRUE(second.simpleResult.has_value());
  EXPECT_EQ(first.simpleResult->iterations, second.simpleResult->iterations);
  EXPECT_DOUBLE_EQ(first.simpleResult->finalUResidual, second.simpleResult->finalUResidual);
}

// Section 13/14: a cancellation requested on the very first iteration
// stops the solve early with Cancelled -- not silently reinterpreted as
// MaxIterations/Converged -- and progressCallback fires at least once
// before that happens.
TEST(ProjectRunnerTest, CancellationStopsEarlyWithCancelledStatus) {
  int progressCalls = 0;
  cfd::app::ProjectRunOptions options;
  options.progressCallback = [&progressCalls](const cfd::pressure_velocity::SIMPLEIterationProgress&) {
    ++progressCalls;
  };
  options.cancellationCheck = [&progressCalls]() { return progressCalls >= 2; };

  const ProjectRunResult run = ProjectRunner::run("tests/data/cases/valid_cavity", options);

  EXPECT_EQ(run.status, ProjectRunStatus::Cancelled);
  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_EQ(run.simpleResult->status, cfd::pressure_velocity::SIMPLEStatus::Cancelled);
  EXPECT_GE(progressCalls, 2);
  EXPECT_LT(run.simpleResult->iterations, 390);  // valid_cavity normally converges around ~390.
  EXPECT_EQ(cfd::app::exitCodeFor(run.status), 5);
}

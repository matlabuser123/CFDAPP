// P5 -- Application, section 9 ("CLI/GUI execution equivalence"):
// ProjectRunner is the one production solver backend apps/cli/main.cpp
// and apps/gui's SimulationController both call -- exercised directly
// here (not through either front end) so this suite covers both by
// construction. gtest_discover_tests sets WORKING_DIRECTORY to
// CFDApp_SOURCE_DIR (same convention as tests/integration/case), so the
// relative fixture path below resolves against the repository root.
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"

using cfd::app::ProjectRunner;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::testutil::CaseFixtureCopy;

TEST(ProjectRunnerTest, ValidCaseConvergesAndExportsResults) {
  // P7-TEST-001: a private copy -- every ProjectRunner::run() call in
  // this file writes results/ into whatever directory it's given, and
  // "tests/data/cases/valid_cavity" is also read/written by several
  // other test binaries ctest -j8 can run concurrently (see
  // CaseFixtureCopy.hpp's own header comment).
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  const ProjectRunResult run = ProjectRunner::run(fixture.path());

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
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  const ProjectRunResult first = ProjectRunner::run(fixture.path());
  const ProjectRunResult second = ProjectRunner::run(fixture.path());

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
  options.progressCallback =
      [&progressCalls](const cfd::pressure_velocity::SIMPLEIterationProgress&) { ++progressCalls; };
  options.cancellationCheck = [&progressCalls]() { return progressCalls >= 2; };

  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  const ProjectRunResult run = ProjectRunner::run(fixture.path(), options);

  EXPECT_EQ(run.status, ProjectRunStatus::Cancelled);
  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_EQ(run.simpleResult->status, cfd::pressure_velocity::SIMPLEStatus::Cancelled);
  EXPECT_GE(progressCalls, 2);
  EXPECT_LT(run.simpleResult->iterations, 390);  // valid_cavity normally converges around ~390.
  EXPECT_EQ(cfd::app::exitCodeFor(run.status), 5);
}

// P12-NUM-004: solver.json's "robustness" block end to end through the
// production backend -- statuses map to the documented run statuses/exit
// codes and reach the exported results.
namespace {

void writeFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream out(path);
  out << content;
}

// %.17g, not std::to_string (which prints 1e-16 as "0.000000").
std::string number(double value) {
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  return buffer;
}

std::string robustSolverJson(double alpha, double tolerance, const std::string& robustness) {
  return std::string(R"({
  "type": "SIMPLE", "max_iterations": 6000,
  "velocity_relaxation": )") +
         number(alpha) + R"(, "pressure_relaxation": )" + number(alpha) + R"(,
  "velocity_tolerance": )" +
         number(tolerance) + R"(, "pressure_tolerance": )" + number(tolerance) +
         R"(, "continuity_tolerance": )" + number(tolerance) + R"(,
  "momentum_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                             "relative_tolerance": 1e-8, "max_iterations": 500},
  "pressure_linear_solver": {"type": "BiCGSTAB", "absolute_tolerance": 1e-10,
                             "relative_tolerance": 1e-8, "max_iterations": 2000},
  "robustness": )" +
         robustness + "\n}";
}

}  // namespace

TEST(ProjectRunnerTest, DivergenceDetectionReportsNumericalFailure) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  writeFile(fixture.path() / "mesh.json", R"({"type": "structured_cartesian", "nx": 8, "ny": 8})");
  writeFile(fixture.path() / "solver.json",
            robustSolverJson(0.9, 1e-6, R"({"divergence_detection": {"enabled": true}})"));
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_EQ(run.simpleResult->status, cfd::pressure_velocity::SIMPLEStatus::Diverging);
  EXPECT_EQ(run.status, ProjectRunStatus::NumericalFailure);
  EXPECT_EQ(cfd::app::exitCodeFor(run.status), 4);
  EXPECT_FALSE(run.simpleResult->robustness.statusDetail.empty());
}

TEST(ProjectRunnerTest, StagnationDetectionReportsDidNotConverge) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  writeFile(fixture.path() / "mesh.json", R"({"type": "structured_cartesian", "nx": 8, "ny": 8})");
  writeFile(fixture.path() / "solver.json",
            robustSolverJson(0.7, 1e-16, R"({"stagnation_detection": {"enabled": true}})"));
  // 0.7 for both factors is fine for this 8x8 Re=100 cavity.
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_EQ(run.simpleResult->status, cfd::pressure_velocity::SIMPLEStatus::Stagnated);
  EXPECT_LT(run.simpleResult->iterations, 6000u);
  EXPECT_EQ(run.status, ProjectRunStatus::DidNotConverge);
  EXPECT_EQ(cfd::app::exitCodeFor(run.status), 3);
  ASSERT_TRUE(run.exportSummary.has_value());
  std::ifstream in(run.exportSummary->metadataPath);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("\"Stagnated\""), std::string::npos);
  EXPECT_NE(text.find("\"robustness\""), std::string::npos);
}

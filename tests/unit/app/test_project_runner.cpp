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
#include "cfd/mesh/MeshQuality.hpp"

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

// P12-MESH-004: the production mesh-quality report reaches the run result
// and the exported metadata; a warning never stops the run.
TEST(ProjectRunnerTest, MeshQualityReportIsReturnedAndExported) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  ASSERT_TRUE(run.meshQuality.has_value());
  ASSERT_TRUE(run.mesh.has_value());
  EXPECT_EQ(run.meshQuality->status, cfd::mesh::MeshQualityStatus::Valid);
  EXPECT_EQ(run.meshQuality->cellCount, run.mesh->numberOfCells());
  EXPECT_EQ(run.meshQuality->summaryLine(),
            cfd::mesh::MeshQuality::evaluate(*run.mesh).summaryLine());
  ASSERT_TRUE(run.exportSummary.has_value());
  std::ifstream in(run.exportSummary->metadataPath);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("\"mesh_quality\""), std::string::npos);
  EXPECT_NE(text.find("\"status\": \"valid\""), std::string::npos);
}

TEST(ProjectRunnerTest, MeshQualityWarningDoesNotStopTheRun) {
  const CaseFixtureCopy fixture("tests/data/cases/mesh_quality_warning_cli");
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  EXPECT_EQ(run.status, ProjectRunStatus::Converged);
  ASSERT_TRUE(run.meshQuality.has_value());
  EXPECT_EQ(run.meshQuality->status, cfd::mesh::MeshQualityStatus::ValidWithWarnings);
  EXPECT_EQ(run.meshQuality->expansionRatio.aboveWarning, 8u);
  ASSERT_TRUE(run.exportSummary.has_value());
  std::ifstream in(run.exportSummary->metadataPath);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("\"valid_with_warnings\""), std::string::npos);
  EXPECT_NE(text.find("\"severity\": \"warning\""), std::string::npos);
}

// =================================================================================================
// TD-047 -- a failed REACTING linear solve must reach the run status, and therefore the exit code.
//
// Before this, `out.status` was set once from the FLOW solve and the reacting march could not
// change it, so a reacting case whose every species and energy solve FAILED exited 0. The rule
// lives in a pure function precisely so it can be covered exhaustively here: no test drives the
// reacting path end-to-end, and engineering a reacting solve that genuinely fails is not a fixture
// task. Gate: results/technical_debt/TD047_reacting_failure_escalation/GATE.md
// =================================================================================================

TEST(ProjectRunnerReactingStatusTest, ASucceedingReactingMarchLeavesEveryFlowStatusUntouched) {
  // C1, the `false` half: the escalation must fire ONLY on failure. Without this row the fix
  // could be a blanket downgrade and still pass the failure rows.
  for (const ProjectRunStatus flow :
       {ProjectRunStatus::Converged, ProjectRunStatus::DidNotConverge,
        ProjectRunStatus::NumericalFailure, ProjectRunStatus::Cancelled,
        ProjectRunStatus::InvalidCase, ProjectRunStatus::ApplicationError}) {
    EXPECT_EQ(cfd::app::statusWithReactingOutcome(flow, /*reactingSolveFailed=*/false), flow)
        << "a reacting march with no failed solve changed the flow's verdict";
  }
}

TEST(ProjectRunnerReactingStatusTest, AFailedReactingSolveEscalatesToNumericalFailure) {
  // C1, the `true` half. NumericalFailure is not invented: statusFor(SIMPLEStatus) already maps
  // the FLOW's own MomentumFailure and PressureCorrectionFailure to it.
  for (const ProjectRunStatus flow :
       {ProjectRunStatus::Converged, ProjectRunStatus::DidNotConverge,
        ProjectRunStatus::NumericalFailure, ProjectRunStatus::InvalidCase,
        ProjectRunStatus::ApplicationError}) {
    EXPECT_EQ(cfd::app::statusWithReactingOutcome(flow, /*reactingSolveFailed=*/true),
              ProjectRunStatus::NumericalFailure)
        << "a failed reacting linear solve did not reach the run status";
  }
}

TEST(ProjectRunnerReactingStatusTest, CancellationSurvivesAFailedReactingSolve) {
  // C1's one exception. A cancelled run's reacting failure is a consequence of the cancellation,
  // so Cancelled is the more informative verdict and must not be overwritten.
  EXPECT_EQ(cfd::app::statusWithReactingOutcome(ProjectRunStatus::Cancelled,
                                                /*reactingSolveFailed=*/true),
            ProjectRunStatus::Cancelled);
}

TEST(ProjectRunnerReactingStatusTest, TheExitCodeChangesFromZeroToFour) {
  // C2 -- the defect's headline, stated as the two numbers that differ. A converged flow with a
  // failed reacting march exited 0 before; it must now exit 4, the code the flow's own
  // linear-solve failures already produce.
  const ProjectRunStatus escalated =
      cfd::app::statusWithReactingOutcome(ProjectRunStatus::Converged, true);
  EXPECT_EQ(cfd::app::exitCodeFor(escalated), 4);

  // And the no-failure path still exits 0, so the change cannot turn a good run bad.
  const ProjectRunStatus untouched =
      cfd::app::statusWithReactingOutcome(ProjectRunStatus::Converged, false);
  EXPECT_EQ(cfd::app::exitCodeFor(untouched), 0);
}

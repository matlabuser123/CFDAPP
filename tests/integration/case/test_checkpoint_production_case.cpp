// P13-001 -- Restart / checkpointing through the PRODUCTION path.
//
// CLAUDE.md section 9: a library API is not production capability. The tests
// in tests/integration/restart/test_simple_checkpoint.cpp exercise the solver
// and the format directly; these exercise
//
//     case directory -> CaseReader -> CaseBuilder -> ProjectRunner -> export
//
// which is the exact path both the CLI and the GUI take. The CLI's own flag
// handling is covered by results/p13/restart/tools/cli_checkpoint.sh, which
// runs the shipped binary; what is asserted here is everything below the flag
// parsing, so CI covers it on every push.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/ProjectRunner.hpp"
#include "cfd/io/case/CaseFingerprint.hpp"

namespace {

using cfd::app::ProjectRunner;
using cfd::app::ProjectRunOptions;
using cfd::app::ProjectRunResult;
using cfd::app::ProjectRunStatus;
using cfd::testutil::CaseFixtureCopy;

constexpr const char* kCavity = "tests/data/cases/valid_cavity";

// The fixture's own max_iterations is a full production budget; these tests
// need a short, fixed one so they stay fast and so the split points are
// meaningful.
void setMaxIterations(const std::filesystem::path& caseDirectory, int iterations) {
  const auto path = caseDirectory / "solver.json";
  nlohmann::json solver;
  {
    std::ifstream in(path);
    in >> solver;
  }
  solver["max_iterations"] = iterations;
  std::ofstream out(path);
  out << solver.dump(2) << '\n';
}

void multiplyViscosity(const std::filesystem::path& caseDirectory, double factor) {
  const auto path = caseDirectory / "physics.json";
  nlohmann::json physics;
  {
    std::ifstream in(path);
    in >> physics;
  }
  physics["dynamic_viscosity"] = physics["dynamic_viscosity"].get<double>() * factor;
  std::ofstream out(path);
  out << physics.dump(2) << '\n';
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::filesystem::path checkpointPathFor(const CaseFixtureCopy& fixture) {
  return fixture.path() / "checkpoint.json";
}

// Turns a fixture into a transport-equation turbulence case through the SAME
// case-file key production uses -- physics.json's "turbulence" object, parsed
// by CaseReader and applied by CaseBuilder. No test-only hook sets it, so what
// this drives is the configuration a user can actually write.
void makeTurbulent(const std::filesystem::path& caseDirectory) {
  const auto path = caseDirectory / "physics.json";
  nlohmann::json physics;
  {
    std::ifstream in(path);
    in >> physics;
  }
  // Wall functions OFF: the cavity's corners put y+ outside the high-Re
  // band, and this test is about the refusal, not about the wall law.
  physics["turbulence"] = nlohmann::json{{"model", "k_epsilon"},
                                         {"wall_functions", false},
                                         {"initial_k", 1.0e-3},
                                         {"initial_epsilon", 1.0e-3}};
  std::ofstream out(path);
  out << physics.dump(2) << '\n';
}

}  // namespace

// P13-001 -- THE TURBULENCE REFUSAL, through the production path.
//
// A case carrying transport-equation turbulence cannot be checkpointed: k,
// epsilon/omega and mu_t are not in the format and the models cannot be
// constructed from a saved field, so a resume would continue from the wrong
// turbulence state. ProjectRunner refuses it by name.
//
// Until P16 Step 4 this refusal had no test at all. Its constant and its
// trigger were simply read and believed. That is not evidence, and it matters
// more now than it did: P16 added a TEST-ONLY mature-state injection fixture
// (tests/unit/gpu/KEpsilonModelTestAccess.hpp) whose entire safety argument is
// that no production path reaches it. A refusal with no test is exactly the
// kind of thing that erodes without anyone noticing.
TEST(CheckpointProductionCaseTest, CheckpointingATurbulentCaseIsRefusedByName) {
  CaseFixtureCopy fixture{kCavity};
  setMaxIterations(fixture.path(), 5);
  makeTurbulent(fixture.path());

  ProjectRunOptions options;
  options.checkpointPath = checkpointPathFor(fixture);
  const ProjectRunResult run = ProjectRunner::run(fixture.path(), options);

  EXPECT_EQ(run.status, ProjectRunStatus::ApplicationError);
  EXPECT_NE(run.errorMessage.find("checkpointing is not supported"), std::string::npos)
      << run.errorMessage;
  EXPECT_NE(run.errorMessage.find("k_epsilon"), std::string::npos) << run.errorMessage;

  // Refused BEFORE anything was written. A refusal that still leaves a
  // checkpoint on disk is a trap for the next run.
  EXPECT_EQ(run.checkpointsWritten, 0u);
  EXPECT_FALSE(std::filesystem::exists(checkpointPathFor(fixture)));
}

// Resuming is refused at the other end too, and for the same reason. Asserted
// separately because the two ends are two code paths, and a refusal that
// guards only the write lets a checkpoint from a laminar run be resumed into
// a turbulent one.
TEST(CheckpointProductionCaseTest, ResumingATurbulentCaseIsRefusedByName) {
  CaseFixtureCopy fixture{kCavity};
  setMaxIterations(fixture.path(), 5);
  makeTurbulent(fixture.path());

  ProjectRunOptions options;
  options.resumeFrom = checkpointPathFor(fixture);
  const ProjectRunResult run = ProjectRunner::run(fixture.path(), options);

  EXPECT_EQ(run.status, ProjectRunStatus::ApplicationError);
  EXPECT_NE(run.errorMessage.find("checkpointing is not supported"), std::string::npos)
      << run.errorMessage;
}

// NON-VACUITY. The two tests above would pass just as well if a turbulent case
// failed for ANY reason -- a parse error in makeTurbulent's JSON, an
// unsupported model, a diverging solve. So the same turbulent fixture, with
// the checkpoint options removed and nothing else changed, must run: the
// refusal has to be about checkpointing, not about turbulence.
TEST(CheckpointProductionCaseTest, TheSameTurbulentCaseRunsWithoutCheckpointOptions) {
  CaseFixtureCopy fixture{kCavity};
  setMaxIterations(fixture.path(), 5);
  makeTurbulent(fixture.path());

  const ProjectRunResult run = ProjectRunner::run(fixture.path());

  EXPECT_NE(run.status, ProjectRunStatus::ApplicationError) << run.errorMessage;
  EXPECT_EQ(run.errorMessage.find("checkpointing is not supported"), std::string::npos)
      << run.errorMessage;
  EXPECT_TRUE(run.simpleResult.has_value());
}

// A run that asks for nothing new must behave exactly as before: no file
// touched, no checkpoint reported.
TEST(CheckpointProductionCaseTest, ARunWithoutCheckpointOptionsTouchesNothing) {
  CaseFixtureCopy fixture{kCavity};
  setMaxIterations(fixture.path(), 20);
  const ProjectRunResult run = ProjectRunner::run(fixture.path());
  EXPECT_EQ(run.status, ProjectRunStatus::DidNotConverge);
  EXPECT_EQ(run.checkpointsWritten, 0u);
  EXPECT_EQ(run.resumedFromIteration, 0u);
  EXPECT_FALSE(run.checkpointPath.has_value());
  EXPECT_FALSE(std::filesystem::exists(checkpointPathFor(fixture)));
}

// The decisive production property: a run split by a checkpoint exports the
// same results as an uninterrupted one.
TEST(CheckpointProductionCaseTest, ASplitRunExportsTheSameResultsAsAContinuousRun) {
  CaseFixtureCopy continuous{kCavity};
  setMaxIterations(continuous.path(), 30);
  const ProjectRunResult reference = ProjectRunner::run(continuous.path());
  ASSERT_TRUE(reference.simpleResult.has_value());
  ASSERT_EQ(reference.simpleResult->iterations, 30u);

  CaseFixtureCopy split{kCavity};
  const auto checkpoint = checkpointPathFor(split);

  // Segment 1: a shorter budget, writing a checkpoint at the end.
  setMaxIterations(split.path(), 12);
  ProjectRunOptions writeOptions;
  writeOptions.checkpointPath = checkpoint;
  const ProjectRunResult first = ProjectRunner::run(split.path(), writeOptions);
  ASSERT_TRUE(first.simpleResult.has_value());
  EXPECT_EQ(first.simpleResult->iterations, 12u);
  EXPECT_EQ(first.checkpointsWritten, 1u);
  ASSERT_TRUE(std::filesystem::exists(checkpoint));

  // Segment 2: the budget raised to the full 30 -- the one case key a resume
  // is allowed to change -- and resumed from the checkpoint.
  setMaxIterations(split.path(), 30);
  ProjectRunOptions resumeOptions;
  resumeOptions.resumeFrom = checkpoint;
  const ProjectRunResult second = ProjectRunner::run(split.path(), resumeOptions);
  ASSERT_TRUE(second.simpleResult.has_value());
  EXPECT_EQ(second.resumedFromIteration, 12u);
  EXPECT_TRUE(second.simpleResult->resumedFromContinuation);
  EXPECT_EQ(second.simpleResult->iterations, 30u);
  EXPECT_EQ(second.status, reference.status);

  ASSERT_TRUE(reference.exportSummary.has_value());
  ASSERT_TRUE(second.exportSummary.has_value());
  EXPECT_EQ(readFile(reference.exportSummary->metadataPath),
            readFile(second.exportSummary->metadataPath));
  EXPECT_EQ(readFile(reference.exportSummary->residualsCsvPath),
            readFile(second.exportSummary->residualsCsvPath));
  ASSERT_TRUE(reference.exportSummary->fieldsCsvPath.has_value());
  ASSERT_TRUE(second.exportSummary->fieldsCsvPath.has_value());
  EXPECT_EQ(readFile(*reference.exportSummary->fieldsCsvPath),
            readFile(*second.exportSummary->fieldsCsvPath));
}

// Periodic checkpointing segments the solve. If the segmentation were not
// exact, this is where it would show.
TEST(CheckpointProductionCaseTest, PeriodicCheckpointingDoesNotChangeTheResult) {
  CaseFixtureCopy continuous{kCavity};
  setMaxIterations(continuous.path(), 30);
  const ProjectRunResult reference = ProjectRunner::run(continuous.path());

  CaseFixtureCopy segmented{kCavity};
  setMaxIterations(segmented.path(), 30);
  ProjectRunOptions options;
  options.checkpointPath = checkpointPathFor(segmented);
  options.checkpointInterval = 7;
  const ProjectRunResult run = ProjectRunner::run(segmented.path(), options);

  ASSERT_TRUE(run.simpleResult.has_value());
  EXPECT_EQ(run.simpleResult->iterations, 30u);
  // 30 iterations in segments of 7: four full segments plus a final one of 2,
  // each writing once. The exact count is asserted rather than "> 0" so a
  // silently-changed segmentation is a failure, not a shrug.
  EXPECT_EQ(run.checkpointsWritten, 5u);
  ASSERT_TRUE(reference.exportSummary.has_value());
  ASSERT_TRUE(run.exportSummary.has_value());
  EXPECT_EQ(readFile(*reference.exportSummary->fieldsCsvPath),
            readFile(*run.exportSummary->fieldsCsvPath));
  EXPECT_EQ(readFile(reference.exportSummary->residualsCsvPath),
            readFile(run.exportSummary->residualsCsvPath));
}

TEST(CheckpointProductionCaseTest, ResumingIntoAChangedCaseIsRefused) {
  CaseFixtureCopy source{kCavity};
  setMaxIterations(source.path(), 10);
  const auto checkpoint = checkpointPathFor(source);
  ProjectRunOptions writeOptions;
  writeOptions.checkpointPath = checkpoint;
  ASSERT_EQ(ProjectRunner::run(source.path(), writeOptions).checkpointsWritten, 1u);

  // Same mesh, different physics: only the case fingerprint can catch this.
  CaseFixtureCopy changed{kCavity};
  setMaxIterations(changed.path(), 10);
  multiplyViscosity(changed.path(), 2.0);
  ProjectRunOptions resumeOptions;
  resumeOptions.resumeFrom = checkpoint;
  const ProjectRunResult refused = ProjectRunner::run(changed.path(), resumeOptions);
  EXPECT_EQ(refused.status, ProjectRunStatus::ApplicationError);
  EXPECT_NE(refused.errorMessage.find("case identity mismatch"), std::string::npos)
      << refused.errorMessage;

  // Non-vacuity: the unchanged case resumes from the same file, so what was
  // refused above was the physics change and not the file.
  ProjectRunOptions sameCase;
  sameCase.resumeFrom = checkpoint;
  setMaxIterations(source.path(), 20);
  const ProjectRunResult accepted = ProjectRunner::run(source.path(), sameCase);
  EXPECT_NE(accepted.status, ProjectRunStatus::ApplicationError) << accepted.errorMessage;
  EXPECT_EQ(accepted.resumedFromIteration, 10u);
}

// The half of the write-failure policy that matters most: a checkpoint that
// cannot be written for a solve that SUCCEEDED is reported, never silently
// skipped. (The other half -- a refusal on a solve that already failed leaves
// the solve's own status to speak, so a numerical failure is not masked by an
// I/O message -- has no deterministic case fixture that produces a non-finite
// field; see results/p13/restart/summary.md.)
TEST(CheckpointProductionCaseTest, AnUnwritableCheckpointPathIsReported) {
  CaseFixtureCopy fixture{kCavity};
  setMaxIterations(fixture.path(), 5);
  // A directory is not a file a checkpoint can be written to.
  const auto blocked = fixture.path() / "checkpoint_is_a_directory";
  std::filesystem::create_directories(blocked);

  ProjectRunOptions options;
  options.checkpointPath = blocked;
  const ProjectRunResult run = ProjectRunner::run(fixture.path(), options);
  EXPECT_EQ(run.status, ProjectRunStatus::ApplicationError);
  EXPECT_NE(run.errorMessage.find("could not write checkpoint"), std::string::npos)
      << run.errorMessage;
  EXPECT_EQ(run.checkpointsWritten, 0u);
}

TEST(CheckpointProductionCaseTest, AMissingCheckpointIsReportedNotIgnored) {
  CaseFixtureCopy fixture{kCavity};
  setMaxIterations(fixture.path(), 5);
  ProjectRunOptions options;
  options.resumeFrom = fixture.path() / "no_such_checkpoint.json";
  const ProjectRunResult run = ProjectRunner::run(fixture.path(), options);
  EXPECT_EQ(run.status, ProjectRunStatus::ApplicationError);
  EXPECT_NE(run.errorMessage.find("could not resume"), std::string::npos) << run.errorMessage;
  EXPECT_EQ(cfd::app::exitCodeFor(run.status), 1);
}

// The case fingerprint decides what "the same case" means, so what it does and
// does not cover is a contract worth pinning.
TEST(CaseFingerprintTest, IgnoresDocumentationAndTheIterationBudgetOnly) {
  CaseFixtureCopy fixture{kCavity};
  const std::string original = cfd::io::computeCaseFingerprint(fixture.path());

  setMaxIterations(fixture.path(), 12345);
  EXPECT_EQ(cfd::io::computeCaseFingerprint(fixture.path()), original)
      << "max_iterations is a budget, not part of the problem definition";

  {  // name and description are documentation
    const auto path = fixture.path() / "case.json";
    nlohmann::json manifest;
    {
      std::ifstream in(path);
      in >> manifest;
    }
    manifest["description"] = "a typo fix must not invalidate a running job";
    std::ofstream out(path);
    out << manifest.dump(2) << '\n';
  }
  EXPECT_EQ(cfd::io::computeCaseFingerprint(fixture.path()), original);

  // Everything else does change it.
  multiplyViscosity(fixture.path(), 1.5);
  EXPECT_NE(cfd::io::computeCaseFingerprint(fixture.path()), original);
}

TEST(CaseFingerprintTest, IsInsensitiveToFormattingButNotToValues) {
  CaseFixtureCopy fixture{kCavity};
  const std::string original = cfd::io::computeCaseFingerprint(fixture.path());

  // Re-serialize solver.json with different indentation and key order: the
  // content is identical, so the fingerprint must be too.
  const auto path = fixture.path() / "solver.json";
  nlohmann::json solver;
  {
    std::ifstream in(path);
    in >> solver;
  }
  {
    std::ofstream out(path);
    out << solver.dump(7) << "\n\n\n";
  }
  EXPECT_EQ(cfd::io::computeCaseFingerprint(fixture.path()), original);

  solver["velocity_relaxation"] = solver["velocity_relaxation"].get<double>() * 0.5;
  {
    std::ofstream out(path);
    out << solver.dump(2) << '\n';
  }
  EXPECT_NE(cfd::io::computeCaseFingerprint(fixture.path()), original);
}

// P5-A -- Production Case Manager: CaseSession's own state machine
// (TODO.md P5 section 5) and lifecycle operations (section 3), exercised
// directly against the same CaseReader/CaseWriter/CaseBuilder/
// ProjectRunner pipeline the CLI uses (section 0's "ONE SOLVER
// BACKEND").
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>

#include "CaseFixtureCopy.hpp"
#include "cfd/app/CaseSession.hpp"

using cfd::app::CaseSession;
using cfd::app::CaseState;
using cfd::testutil::CaseFixtureCopy;

namespace {

// A temp directory deleted on scope exit -- same RAII convention as
// tests/unit/io/CaseFixture.hpp's own CaseFixture, kept local here
// rather than sharing that header across test binaries.
class TempDir {
 public:
  TempDir() {
    path_ = std::filesystem::temp_directory_path() /
            ("cfdapp_session_test_" +
             std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
             std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST(CaseSessionTest, StartsEmpty) {
  CaseSession session;
  EXPECT_EQ(session.state(), CaseState::Empty);
  EXPECT_FALSE(session.canRun());
  EXPECT_FALSE(session.canSave());
  EXPECT_FALSE(session.canStop());
}

TEST(CaseSessionTest, NewCaseMovesToLoadedWithNoDirectory) {
  CaseSession session;
  session.newCase();
  EXPECT_EQ(session.state(), CaseState::Loaded);
  EXPECT_FALSE(session.directory().has_value());
  ASSERT_TRUE(session.caseDefinition().has_value());
  EXPECT_TRUE(
      session.canSave());  // save() itself will still fail (no directory) -- saveAs() works.
  EXPECT_TRUE(
      session.canRun());  // canRun() does not require a directory (run() checks that itself).
}

TEST(CaseSessionTest, OpenValidCaseMovesToLoaded) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  CaseSession session;
  ASSERT_TRUE(session.open(fixture.path()));
  EXPECT_EQ(session.state(), CaseState::Loaded);
  ASSERT_TRUE(session.caseDefinition().has_value());
  EXPECT_EQ(session.caseDefinition()->mesh.nx, 4u);
  EXPECT_TRUE(session.canRun());
  EXPECT_TRUE(session.canSave());
}

TEST(CaseSessionTest, OpenMissingDirectoryLeavesStateUnchangedAndSetsError) {
  CaseSession session;
  EXPECT_FALSE(session.open("this/directory/does/not/exist"));
  EXPECT_EQ(session.state(), CaseState::Empty);
  EXPECT_FALSE(session.lastError().empty());
}

TEST(CaseSessionTest, SetCaseDefinitionMarksModified) {
  CaseSession session;
  session.newCase();
  cfd::io::CaseDefinition edited = *session.caseDefinition();
  edited.mesh.nx = 8;
  session.setCaseDefinition(edited);

  EXPECT_EQ(session.state(), CaseState::Modified);
  EXPECT_TRUE(session.isModified());
  EXPECT_EQ(session.caseDefinition()->mesh.nx, 8u);
}

TEST(CaseSessionTest, SaveAsWritesARoundTripLoadableCase) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  TempDir target;
  CaseSession source;
  ASSERT_TRUE(source.open(fixture.path()));

  ASSERT_TRUE(source.saveAs(target.path()));
  EXPECT_EQ(source.directory(), target.path());

  // A CLI-created case must be loadable by the GUI and vice versa
  // (section 4) -- checked here by having a second, independent session
  // open what the first just wrote.
  CaseSession reopened;
  ASSERT_TRUE(reopened.open(target.path()));
  EXPECT_EQ(reopened.caseDefinition()->mesh.nx, source.caseDefinition()->mesh.nx);
  EXPECT_EQ(reopened.caseDefinition()->caseConfig.name, source.caseDefinition()->caseConfig.name);
}

TEST(CaseSessionTest, SaveWithoutADirectorySetFails) {
  CaseSession session;
  session.newCase();
  EXPECT_FALSE(session.save());
  EXPECT_FALSE(session.lastError().empty());
}

TEST(CaseSessionTest, ReloadDiscardsInMemoryEdits) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  TempDir target;
  CaseSession session;
  ASSERT_TRUE(session.open(fixture.path()));
  ASSERT_TRUE(session.saveAs(target.path()));

  cfd::io::CaseDefinition edited = *session.caseDefinition();
  edited.caseConfig.name = "Edited In Memory Only";
  session.setCaseDefinition(edited);
  EXPECT_EQ(session.state(), CaseState::Modified);

  ASSERT_TRUE(session.reload());
  EXPECT_EQ(session.state(), CaseState::Loaded);
  EXPECT_NE(session.caseDefinition()->caseConfig.name, "Edited In Memory Only");
}

TEST(CaseSessionTest, ValidateValidCaseMovesToValidated) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  CaseSession session;
  ASSERT_TRUE(session.open(fixture.path()));
  ASSERT_TRUE(session.validate());
  EXPECT_EQ(session.state(), CaseState::Validated);
}

TEST(CaseSessionTest, ValidateInvalidCaseLeavesStateAndReportsError) {
  CaseSession session;
  session.newCase();  // fresh, empty case: zero density/viscosity, no boundary patches.
  EXPECT_FALSE(session.validate());
  EXPECT_EQ(session.state(), CaseState::Loaded);
  EXPECT_FALSE(session.lastError().empty());
}

TEST(CaseSessionTest, RunConvergingCaseMovesToCompleted) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  CaseSession session;
  ASSERT_TRUE(session.open(fixture.path()));
  const auto result = session.run();

  EXPECT_EQ(session.state(), CaseState::Completed);
  EXPECT_EQ(result.status, cfd::app::ProjectRunStatus::Converged);
  ASSERT_TRUE(session.lastRun().has_value());
  EXPECT_EQ(session.lastRun()->status, cfd::app::ProjectRunStatus::Converged);
}

TEST(CaseSessionTest, RunWithoutADirectoryFails) {
  CaseSession session;
  session.newCase();
  const auto result = session.run();
  EXPECT_EQ(result.status, cfd::app::ProjectRunStatus::ApplicationError);
  EXPECT_EQ(session.state(), CaseState::Loaded);  // never entered Running.
}

TEST(CaseSessionTest, RequestCancelStopsARunEarly) {
  const CaseFixtureCopy fixture("tests/data/cases/valid_cavity");
  CaseSession session;
  ASSERT_TRUE(session.open(fixture.path()));

  int progressCalls = 0;
  cfd::app::ProjectRunOptions options;
  options.progressCallback =
      [&session, &progressCalls](const cfd::pressure_velocity::SIMPLEIterationProgress&) {
        ++progressCalls;
        if (progressCalls == 2) session.requestCancel();
      };

  const auto result = session.run(options);
  EXPECT_EQ(result.status, cfd::app::ProjectRunStatus::Cancelled);
  EXPECT_EQ(session.state(), CaseState::Cancelled);
  EXPECT_FALSE(session.canStop());
}

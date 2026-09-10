#pragma once

#include <filesystem>
#include <string>

namespace cfd::testutil {

// P7-TEST-001: a private, per-test copy of an entire canonical case
// fixture directory (e.g. "tests/data/cases/valid_cavity") -- closes a
// real, observed parallel-ctest race: many test binaries across
// tests/unit/app, tests/integration/case, tests/integration/io, and
// apps/gui/tests all independently call ProjectRunner::run()/
// CaseSession::run()/SimulationController::run() against that exact same
// shared, git-tracked directory, each one rewriting its results/
// subtree (fields.csv/metadata.json/residuals.csv/solution.vtk) via
// ResultExporter. Under `ctest -j8`, several of those binaries run as
// genuinely concurrent OS processes, all reading and writing one shared
// path at once -- observed as an intermittent CaseReader read failure
// (ProjectRunStatus::InvalidCase) even though the six canonical *.json
// input files are never intentionally mutated by any test, consistent
// with a filesystem-level race under concurrent access to one shared
// directory (never reproduces when the failing test is rerun alone).
//
// The fix is not "figure out exactly which access pattern is safe to
// leave shared" -- it is "make every test's own copy of the fixture
// private", full stop: construct one of these with the canonical fixture
// directory, then pass path() (or rely on the implicit std::string/
// std::filesystem::path conversions below) everywhere a test previously
// passed the shared path literal. The copy is a real, independent
// directory tree (not a symlink) so a test's own ProjectRunner::run()
// writing into "<copy>/results/" can never race another process's write
// into the *same* inode -- there is no shared inode anymore. Removed
// (best-effort, recursively) on destruction, the same "always clean up,
// even on an ASSERT_* early return" RAII convention as
// tests/unit/app/test_case_session.cpp's own local TempDir and
// tests/unit/io/CaseFixture.hpp's own CaseFixture.
class CaseFixtureCopy {
 public:
  // Throws std::filesystem::filesystem_error if `canonicalCaseDirectory`
  // does not exist or the copy fails -- a test fixture that cannot even
  // be copied is a test-environment problem worth failing loudly on, not
  // silently degrading to "share the original after all".
  explicit CaseFixtureCopy(const std::filesystem::path& canonicalCaseDirectory);
  ~CaseFixtureCopy();

  CaseFixtureCopy(const CaseFixtureCopy&) = delete;
  CaseFixtureCopy& operator=(const CaseFixtureCopy&) = delete;
  // Move-only: exactly one owner ever removes the directory.
  CaseFixtureCopy(CaseFixtureCopy&& other) noexcept;
  CaseFixtureCopy& operator=(CaseFixtureCopy&& other) noexcept;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  // Deliberately implicit so an existing call site reads exactly like
  // the string/path literal it replaces (e.g. ProjectRunner::run(fixture)
  // instead of ProjectRunner::run(fixture.path())).
  operator std::filesystem::path() const { return path_; }
  operator std::string() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

}  // namespace cfd::testutil

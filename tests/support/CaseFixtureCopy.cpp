#include "CaseFixtureCopy.hpp"

#include <random>
#include <sstream>
#include <system_error>
#include <utility>

namespace cfd::testutil {

namespace {

std::filesystem::path uniqueDestination(const std::filesystem::path& canonicalCaseDirectory) {
  // A random suffix (not a PID or test name) is enough here: collisions
  // are astronomically unlikely and irrelevant even if they somehow
  // occurred (std::filesystem::copy below would simply fail loudly,
  // exactly this class's own documented "fail loudly" contract) -- no
  // need to thread a gtest test name through this Qt-free, gtest-free
  // header (it is shared by plain-gtest and Qt+gtest binaries alike).
  std::random_device rd;
  std::ostringstream name;
  name << "cfdapp_fixture_" << canonicalCaseDirectory.filename().string() << "_" << rd() << "_"
       << rd();
  return std::filesystem::temp_directory_path() / name.str();
}

}  // namespace

CaseFixtureCopy::CaseFixtureCopy(const std::filesystem::path& canonicalCaseDirectory)
    : path_(uniqueDestination(canonicalCaseDirectory)) {
  if (!std::filesystem::is_directory(canonicalCaseDirectory)) {
    throw std::filesystem::filesystem_error(
        "CaseFixtureCopy: canonical case directory does not exist", canonicalCaseDirectory,
        std::make_error_code(std::errc::no_such_file_or_directory));
  }
  // recursive + copy_symlinks (default) -- this fixture tree never
  // contains a symlink in practice, but copying one *as* a symlink
  // rather than following it is the conservative choice regardless.
  std::filesystem::copy(canonicalCaseDirectory, path_, std::filesystem::copy_options::recursive);
}

CaseFixtureCopy::~CaseFixtureCopy() {
  if (path_.empty()) return;  // moved-from.
  std::error_code ec;
  std::filesystem::remove_all(path_, ec);  // best-effort; a leftover temp dir is harmless.
}

CaseFixtureCopy::CaseFixtureCopy(CaseFixtureCopy&& other) noexcept
    : path_(std::exchange(other.path_, std::filesystem::path{})) {}

CaseFixtureCopy& CaseFixtureCopy::operator=(CaseFixtureCopy&& other) noexcept {
  if (this != &other) {
    if (!path_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(path_, ec);
    }
    path_ = std::exchange(other.path_, std::filesystem::path{});
  }
  return *this;
}

}  // namespace cfd::testutil

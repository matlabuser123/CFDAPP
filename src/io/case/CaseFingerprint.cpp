#include "cfd/io/case/CaseFingerprint.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <system_error>

#include "JsonUtil.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::io {

namespace {

// The same construction cfd::mesh::computeMeshFingerprint uses, so the two
// fingerprints in a checkpoint read the same way and neither invents a second
// hashing convention.
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= static_cast<std::uint64_t>(bytes[i]);
    hash *= kFnvPrime;
  }
}

void hashString(std::uint64_t& hash, const std::string& text) {
  hashBytes(hash, text.data(), text.size());
  // A separator, so concatenating two documents cannot collide with a
  // different split of the same bytes.
  const unsigned char separator = 0x1e;
  hashBytes(hash, &separator, 1);
}

// Canonical serialization: nlohmann's object is an ordered map, so dump()
// already emits keys in a fixed order regardless of how the file was written.
// Parsing and re-dumping is what makes this insensitive to whitespace,
// key order and numeric formatting while staying sensitive to every value.
std::string canonical(const nlohmann::json& document) { return document.dump(); }

// Exactly CaseReader's own manifest resolution, including its refusal of a
// reference that escapes the case directory. Duplicated deliberately rather
// than exported: CaseReader's copy is the authority on what a case IS, and
// this file must not be able to change that by being edited.
std::filesystem::path resolveCaseLocalFile(const std::filesystem::path& caseDirectory,
                                           const std::filesystem::path& manifestPath,
                                           std::string_view referenceField,
                                           const std::string& referenceValue) {
  const std::filesystem::path candidate = (caseDirectory / referenceValue).lexically_normal();
  const std::filesystem::path normalizedCaseDirectory = caseDirectory.lexically_normal();
  const auto mismatch =
      std::mismatch(normalizedCaseDirectory.begin(), normalizedCaseDirectory.end(),
                    candidate.begin(), candidate.end());
  if (mismatch.first != normalizedCaseDirectory.end()) {
    detail::throwConfigError(manifestPath, referenceField,
                             "reference a file inside the case directory", referenceValue);
  }
  std::error_code existsError;
  if (!std::filesystem::exists(candidate, existsError) || existsError) {
    throw IOError("required " + std::string(referenceField) +
                  " configuration not found: " + candidate.string());
  }
  return candidate;
}

}  // namespace

std::string computeCaseFingerprint(const std::filesystem::path& caseDirectory) {
  std::error_code isDirError;
  if (!std::filesystem::is_directory(caseDirectory, isDirError) || isDirError) {
    throw IOError("case directory not found: " + caseDirectory.string());
  }

  const std::filesystem::path manifestPath = caseDirectory / "case.json";
  nlohmann::json manifest = detail::readJsonFile(manifestPath);
  detail::requireObject(manifest, manifestPath);

  std::uint64_t hash = kFnvOffsetBasis;

  // case.json without its two documentation keys -- see the header comment.
  {
    nlohmann::json meaningful = manifest;
    meaningful.erase("name");
    meaningful.erase("description");
    hashString(hash, canonical(meaningful));
  }

  // The five referenced documents, in a fixed order that does not depend on
  // the manifest's own key order.
  static constexpr std::array<std::string_view, 5> kReferences{"geometry", "mesh", "physics",
                                                               "boundaries", "solver"};
  for (std::string_view field : kReferences) {
    const std::string reference = detail::getRequiredString(manifest, manifestPath, field, field);
    const std::filesystem::path path =
        resolveCaseLocalFile(caseDirectory, manifestPath, field, reference);
    nlohmann::json document = detail::readJsonFile(path);
    if (field == "solver" && document.is_object()) {
      // The one excluded numerical key. See the header comment for why a
      // budget is not part of the problem definition.
      document.erase("max_iterations");
    }
    hashString(hash, canonical(document));
  }

  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << hash;
  return out.str();
}

}  // namespace cfd::io

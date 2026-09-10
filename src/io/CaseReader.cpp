#include "cfd/io/CaseReader.hpp"

#include <algorithm>
#include <array>
#include <string>

#include "case/JsonUtil.hpp"
#include "case/Parsers.hpp"

namespace cfd::io {

using cfd::io::detail::getRequiredString;
using cfd::io::detail::readJsonFile;
using cfd::io::detail::requireObject;
using cfd::io::detail::throwConfigError;

namespace {

// The only patch set the currently-supported geometry/mesh combination
// (rectangle + structured_cartesian) produces -- see
// MeshGeometry::createCartesian2D, which always names its four boundary
// patches this way. Cross-file validation (TODO.md P1 section 42)
// compares boundaries.json's configured patches against this fixed set
// rather than needing to actually build a Mesh first.
constexpr std::array<std::string_view, 4> kExpectedPatches{"left", "right", "bottom", "top"};

// Resolves `referenceValue` (from case.json, e.g. "mesh.json") against
// `caseDirectory`, rejecting anything that would escape it (TODO.md P1
// section 37: case-local configuration only, no "../../../something").
// Throws CaseConfigurationError on escape, IOError if the resolved file
// does not exist.
std::filesystem::path resolveCaseLocalFile(const std::filesystem::path& caseDirectory,
                                           const std::filesystem::path& manifestPath,
                                           std::string_view referenceField,
                                           const std::string& referenceValue) {
  const std::filesystem::path candidate = (caseDirectory / referenceValue).lexically_normal();
  const std::filesystem::path normalizedCaseDirectory = caseDirectory.lexically_normal();
  // A lexically-normalized child path must literally begin with the
  // (also normalized) parent directory path -- lexically_normal already
  // collapses "..", so this is enough to catch an escaping reference
  // without needing the file to exist yet (canonical() would require
  // that).
  const auto mismatch =
      std::mismatch(normalizedCaseDirectory.begin(), normalizedCaseDirectory.end(),
                    candidate.begin(), candidate.end());
  if (mismatch.first != normalizedCaseDirectory.end()) {
    throwConfigError(manifestPath, referenceField, "reference a file inside the case directory",
                     referenceValue);
  }
  std::error_code existsError;
  if (!std::filesystem::exists(candidate, existsError) || existsError) {
    throw IOError("required " + std::string(referenceField) +
                  " configuration not found: " + candidate.string());
  }
  return candidate;
}

}  // namespace

CaseDefinition CaseReader::read(const std::filesystem::path& caseDirectory) const {
  std::error_code isDirError;
  if (!std::filesystem::is_directory(caseDirectory, isDirError) || isDirError) {
    throw IOError("case directory not found: " + caseDirectory.string());
  }

  const std::filesystem::path manifestPath = caseDirectory / "case.json";
  const nlohmann::json manifest = readJsonFile(manifestPath);
  requireObject(manifest, manifestPath);

  CaseDefinition definition;
  definition.caseConfig = detail::parseCaseConfig(manifest, manifestPath);
  definition.initialConditions = detail::parseInitialConditions(manifest, manifestPath);

  auto loadReferenced = [&](std::string_view field) {
    const std::string referenceValue = getRequiredString(manifest, manifestPath, field);
    return resolveCaseLocalFile(caseDirectory, manifestPath, field, referenceValue);
  };

  const std::filesystem::path geometryPath = loadReferenced("geometry");
  definition.geometry = detail::parseGeometryConfig(readJsonFile(geometryPath), geometryPath);

  const std::filesystem::path meshPath = loadReferenced("mesh");
  definition.mesh = detail::parseMeshConfig(readJsonFile(meshPath), meshPath);

  const std::filesystem::path physicsPath = loadReferenced("physics");
  definition.physics = detail::parsePhysicsConfig(readJsonFile(physicsPath), physicsPath);

  const std::filesystem::path boundariesPath = loadReferenced("boundaries");
  // physics.json is already parsed above -- its "thermal" presence
  // decides whether boundaries.json's per-patch "temperature" key is
  // required or forbidden (P2-THERMAL-004).
  definition.boundaries = detail::parseBoundaryConfig(readJsonFile(boundariesPath), boundariesPath,
                                                      definition.physics.thermal.has_value());

  const std::filesystem::path solverPath = loadReferenced("solver");
  definition.solver = detail::parseSolverConfig(readJsonFile(solverPath), solverPath);

  // --- Cross-file validation (TODO.md P1 section 42) ----------------------
  // boundaries.json must configure exactly the four patches the only
  // supported geometry/mesh combination produces -- no fewer (an
  // unconfigured patch would otherwise reach SIMPLE with no BC at all),
  // no more (a patch name that will never match anything real, most
  // likely a typo).
  for (std::string_view expected : kExpectedPatches) {
    if (!definition.boundaries.patches.contains(expected)) {
      throwConfigError(boundariesPath, std::string("patches.") + std::string(expected),
                       "be configured (every patch of the generated mesh needs a boundary "
                       "condition)",
                       std::string("missing"));
    }
  }
  if (definition.boundaries.patches.size() != kExpectedPatches.size()) {
    for (const auto& [name, unused] : definition.boundaries.patches) {
      (void)unused;
      const bool known = std::find(kExpectedPatches.begin(), kExpectedPatches.end(), name) !=
                         kExpectedPatches.end();
      if (!known) {
        throwConfigError(
            boundariesPath, "patches." + name,
            "name a patch that exists on the generated mesh (left, right, bottom, top)", name);
      }
    }
  }

  return definition;
}

}  // namespace cfd::io

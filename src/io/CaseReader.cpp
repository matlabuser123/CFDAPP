#include "cfd/io/CaseReader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "case/JsonUtil.hpp"
#include "case/Parsers.hpp"

namespace cfd::io {

using cfd::io::detail::getRequiredString;
using cfd::io::detail::readJsonFile;
using cfd::io::detail::requireObject;
using cfd::io::detail::throwConfigError;

namespace {

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
  // P12-MESH-006: the case dimension (3 for a "box" geometry) fixes the
  // component count of every velocity in the case files.
  const int dimension = geometryDimension(definition.geometry);
  const int icComponents = definition.initialConditions.velocityComponents;
  if (icComponents != 0 && icComponents != dimension) {
    throwConfigError(manifestPath, "initial_conditions.velocity",
                     dimension == 3
                         ? "be an array of exactly 3 numbers [u, v, w] (geometry.json is a 3D box)"
                         : "be an array of exactly 2 numbers [u, v] (a 2D case; [u, v, w] needs a "
                           "geometry.json box)",
                     std::to_string(icComponents) + " components");
  }

  const std::filesystem::path meshPath = loadReferenced("mesh");
  definition.mesh = detail::parseMeshConfig(readJsonFile(meshPath), meshPath);

  const std::filesystem::path physicsPath = loadReferenced("physics");
  definition.physics = detail::parsePhysicsConfig(readJsonFile(physicsPath), physicsPath);

  const std::filesystem::path boundariesPath = loadReferenced("boundaries");
  // physics.json is already parsed above -- its "thermal" presence
  // decides whether boundaries.json's per-patch "temperature" key is
  // required or forbidden (P2-THERMAL-004), its "species" array
  // (P6-PHYS-001) decides the exact per-patch "species" key set required,
  // and its "multiphase" presence (P6-PHYS-002) decides whether each
  // patch's "alpha" key is required or forbidden.
  std::vector<std::string> speciesNames;
  speciesNames.reserve(definition.physics.species.size());
  for (const auto& species : definition.physics.species) speciesNames.push_back(species.name);
  definition.boundaries = detail::parseBoundaryConfig(
      readJsonFile(boundariesPath), boundariesPath, definition.physics.thermal.has_value(),
      speciesNames, definition.physics.multiphase.has_value(), dimension);

  const std::filesystem::path solverPath = loadReferenced("solver");
  definition.solver = detail::parseSolverConfig(readJsonFile(solverPath), solverPath);

  // --- Cross-file validation (TODO.md P1 section 42) ----------------------
  // P12-MESH-006: a 3D case is a geometry.json "box" meshed by a
  // structured_cartesian mesh with nz; nz exists only for a box.
  if (dimension == 3) {
    if (definition.mesh.type != "structured_cartesian") {
      throwConfigError(meshPath, "type",
                       "be structured_cartesian for a geometry.json box (the only 3D mesh)",
                       definition.mesh.type);
    }
    if (definition.mesh.nz == 0) {
      throwConfigError(meshPath, "nz",
                       "be given (cells along z) for a geometry.json box (a 3D case)",
                       std::string("missing"));
    }
  } else if (definition.mesh.nz > 0) {
    throwConfigError(meshPath, "nz", "be absent unless geometry.json type is box (a 3D case)",
                     std::to_string(definition.mesh.nz));
  }
  // P12-MESH-006: the 3D production solver is laminar incompressible flow
  // (SIMPLE with u, v, w); the other physics modules are two-dimensional or
  // not validated in 3D and are refused here, before anything is built.
  if (dimension == 3) {
    const auto refuse = [&](const char* field) {
      throwConfigError(physicsPath, field,
                       "be absent for a 3D (box) case: 3D supports laminar incompressible flow "
                       "only (P12-MESH-006)",
                       std::string("present"));
    };
    // buoyancy before thermal: buoyancy requires thermal, so naming it is the more specific error.
    if (definition.physics.buoyancy.has_value()) refuse("buoyancy");
    if (definition.physics.thermal.has_value()) refuse("thermal");
    if (definition.physics.turbulence.has_value() &&
        definition.physics.turbulence->model != "laminar") {
      refuse("turbulence");
    }
    if (!definition.physics.species.empty()) refuse("species");
    if (definition.physics.multiphase.has_value()) refuse("multiphase");
    if (definition.physics.compressible.has_value()) refuse("compressible");
  }
  // P12-MESH-003: a multiblock mesh defines its own domain, so geometry.json
  // must say so ("mesh_defined"), and only a multiblock mesh can have a
  // mesh_defined geometry.
  if ((definition.mesh.type == "multiblock") != (definition.geometry.type == "mesh_defined")) {
    throwConfigError(geometryPath, "type",
                     "be mesh_defined exactly when mesh.json type is multiblock (mesh type " +
                         definition.mesh.type + ")",
                     definition.geometry.type);
  }
  // P12-MESH-002: a graded structured_cartesian mesh is realised against
  // the geometry.json lengths here, before any mesh is built, so an
  // unusable distribution (overflow, a non-positive or too-small cell
  // width -- cfd::mesh::gradedNodeCoordinates) is a mesh.json error on the
  // offending axis's ratio, never a silently different mesh.
  if (definition.mesh.grading.has_value()) {
    const auto checkAxis = [&](const cfd::mesh::AxisGrading& grading, Index cells, Real length,
                               const char* label) {
      try {
        (void)cfd::mesh::gradedNodeCoordinates(cells, length, grading);
      } catch (const InvalidArgumentError& e) {
        throwConfigError(meshPath, label, std::string("give a usable distribution: ") + e.what(),
                         std::to_string(grading.ratio));
      }
    };
    checkAxis(definition.mesh.grading->x, definition.mesh.nx, definition.geometry.length,
              "grading.x.ratio");
    checkAxis(definition.mesh.grading->y, definition.mesh.ny, definition.geometry.height,
              "grading.y.ratio");
  }
  // P12-MESH-001: a structured_quad mesh must discretise exactly the
  // geometry.json rectangle -- every boundary vertex on its edge (to a
  // relative 1e-9 of the domain size), corners at the corners, and each edge
  // traversed monotonically -- so the four boundary patches are the
  // rectangle's four sides. (Cell-level validity -- convex,
  // counter-clockwise, non-degenerate -- is checked by CaseBuilder when the
  // mesh is built.)
  if (definition.mesh.type == "structured_quad") {
    const auto& mesh = definition.mesh;
    const Real length = definition.geometry.length;
    const Real height = definition.geometry.height;
    const Real tolerance = 1e-9 * std::max(length, height);
    const auto vertexAt = [&](Index i, Index j) -> const Vector2& {
      return mesh.vertices[(j * (mesh.nx + 1)) + i];
    };
    const auto fail = [&](Index i, Index j, const std::string& constraint) {
      const Vector2& v = vertexAt(i, j);
      throwConfigError(
          meshPath, "vertices[" + std::to_string((j * (mesh.nx + 1)) + i) + "]",
          "(vertex i=" + std::to_string(i) + ", j=" + std::to_string(j) + ") " + constraint,
          "[" + std::to_string(v.x) + ", " + std::to_string(v.y) + "]");
    };
    for (Index i = 0; i <= mesh.nx; ++i) {
      if (std::abs(vertexAt(i, 0).y) > tolerance) fail(i, 0, "lie on the bottom edge y = 0");
      if (std::abs(vertexAt(i, mesh.ny).y - height) > tolerance) {
        fail(i, mesh.ny, "lie on the top edge y = height");
      }
    }
    for (Index j = 0; j <= mesh.ny; ++j) {
      if (std::abs(vertexAt(0, j).x) > tolerance) fail(0, j, "lie on the left edge x = 0");
      if (std::abs(vertexAt(mesh.nx, j).x - length) > tolerance) {
        fail(mesh.nx, j, "lie on the right edge x = length");
      }
    }
    for (Index i = 1; i <= mesh.nx; ++i) {
      if (!(vertexAt(i, 0).x > vertexAt(i - 1, 0).x)) {
        fail(i, 0, "increase strictly in x along the bottom edge");
      }
      if (!(vertexAt(i, mesh.ny).x > vertexAt(i - 1, mesh.ny).x)) {
        fail(i, mesh.ny, "increase strictly in x along the top edge");
      }
    }
    for (Index j = 1; j <= mesh.ny; ++j) {
      if (!(vertexAt(0, j).y > vertexAt(0, j - 1).y)) {
        fail(0, j, "increase strictly in y along the left edge");
      }
      if (!(vertexAt(mesh.nx, j).y > vertexAt(mesh.nx, j - 1).y)) {
        fail(mesh.nx, j, "increase strictly in y along the right edge");
      }
    }
    if (std::abs(vertexAt(0, 0).x) > tolerance ||
        std::abs(vertexAt(mesh.nx, 0).x - length) > tolerance ||
        std::abs(vertexAt(0, mesh.ny).x) > tolerance ||
        std::abs(vertexAt(mesh.nx, mesh.ny).x - length) > tolerance) {
      fail(0, 0, "put the four corner vertices at the rectangle corners");
    }
  }

  // boundaries.json must configure exactly the four patches the only
  // supported geometry/mesh combination produces -- no fewer (an
  // unconfigured patch would otherwise reach SIMPLE with no BC at all),
  // no more (a patch name that will never match anything real, most
  // likely a typo).
  // P12-MESH-003: the expected names are the mesh's own -- the four
  // canonical patches, or a multiblock mesh's named patches.
  const std::vector<std::string> expectedPatches = meshPatchNames(definition.mesh);
  for (const std::string& expected : expectedPatches) {
    if (!definition.boundaries.patches.contains(expected)) {
      throwConfigError(boundariesPath, "patches." + expected,
                       "be configured (every patch of the generated mesh needs a boundary "
                       "condition)",
                       std::string("missing"));
    }
  }
  if (definition.boundaries.patches.size() != expectedPatches.size()) {
    std::string list;
    for (const auto& name : expectedPatches) list += (list.empty() ? "" : ", ") + name;
    for (const auto& [name, unused] : definition.boundaries.patches) {
      (void)unused;
      const bool known =
          std::find(expectedPatches.begin(), expectedPatches.end(), name) != expectedPatches.end();
      if (!known) {
        throwConfigError(boundariesPath, "patches." + name,
                         "name a patch that exists on the generated mesh (" + list + ")", name);
      }
    }
  }

  return definition;
}

}  // namespace cfd::io

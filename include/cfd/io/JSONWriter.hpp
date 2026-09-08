#pragma once

// P1 -- Result Export: deterministic run metadata as JSON. Deliberately
// independent of the case-loading pipeline (cfd::io::case::*) -- this
// header only needs the small set of descriptive fields a metadata.json
// actually carries (RunMetadata below), not a full CaseDefinition, so it
// stays usable (and unit-testable) without a case directory on disk.

#include <filesystem>
#include <string>

#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::io {

// The descriptive (non-solver-derived) fields metadata.json needs.
// Everything else in the file (mesh cell/face counts, solver status/
// iterations/residuals/mass imbalance) comes directly from Mesh/
// SIMPLEResult, never duplicated here.
struct RunMetadata {
  std::string caseName;
  cfd::Real density{};
  cfd::Real dynamicViscosity{};
  std::string solverType;
};

class JSONWriter {
 public:
  // Writes metadata.json (section 11-16): format_version, case/mesh/
  // physics/solver metadata, the explicit status enum name (never just a
  // boolean -- section 12), final residuals, global mass imbalance, and
  // a finite/non-finite flag -- as real JSON numbers, not stringified
  // (section 16), with stable 2-space-indented key ordering (section 15).
  // nx/ny are inferred from `mesh` itself (StructuredMeshInfo, same
  // convention VTKWriter uses), not re-supplied by the caller. No
  // timestamp/runtime/absolute-path fields are written (section 33/14:
  // those would make byte-identical repeated-run comparison meaningless).
  // Throws IOError if `path` cannot be opened.
  static void writeMetadata(const std::filesystem::path& path, const RunMetadata& metadata,
                            const cfd::mesh::Mesh& mesh,
                            const cfd::pressure_velocity::SIMPLEResult& result);
};

}  // namespace cfd::io

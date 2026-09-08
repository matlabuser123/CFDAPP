#pragma once

// Restart-D (TODO.md P2 -- Restart capability): the restart file reader.
// Reads exactly the schema RestartWriter.hpp writes.

#include <filesystem>

#include "cfd/mesh/Mesh.hpp"
#include "cfd/solver/RestartSnapshot.hpp"

namespace cfd::io {

class RestartReader {
 public:
  // Reads `path` and returns a RestartSnapshot -- run through
  // validateRestartSnapshot(snapshot, mesh) before returning, so a
  // caller never receives an invalid snapshot: no partial acceptance of
  // a corrupt file, and `mesh` is exactly the mesh the caller intends to
  // resume on (mesh-identity mismatch is rejected here, not left to the
  // caller to remember to check separately).
  //
  // Throws IOError if `path` does not exist, cannot be opened, or is not
  // well-formed JSON. Throws CaseConfigurationError if the JSON is
  // well-formed but does not match this file's schema (missing field,
  // wrong JSON type, wrong field-array length). Throws
  // InvalidArgumentError if the parsed content fails
  // validateRestartSnapshot (unsupported format version, non-finite
  // value, mesh-identity mismatch, ...) -- the same exception type and
  // conditions constructing a RestartSnapshot any other way would raise.
  [[nodiscard]] static cfd::solver::RestartSnapshot read(const std::filesystem::path& path,
                                                         const cfd::mesh::Mesh& mesh);
};

}  // namespace cfd::io

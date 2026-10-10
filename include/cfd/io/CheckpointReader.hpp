#pragma once

// P13-001 -- Restart / checkpointing: the production checkpoint file reader.
// Reads exactly the schema CheckpointWriter.hpp writes, and accepts nothing
// else. Mirrors RestartReader.hpp's own contract.

#include <filesystem>
#include <string>

#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLECheckpoint.hpp"

namespace cfd::io {

class CheckpointReader {
 public:
  // Reads `path` and returns a checkpoint that has already passed
  // validateSIMPLECheckpoint(checkpoint, mesh) -- so a caller never receives
  // one that is corrupt, incomplete, non-finite, or built for a different
  // mesh. There is no partial acceptance: either the whole document is a
  // valid checkpoint for `mesh`, or nothing is returned.
  //
  //   IOError                 `path` is missing, unreadable, or not
  //                           well-formed JSON (a truncated file lands here).
  //   CaseConfigurationError  well-formed JSON that is not this schema: a
  //                           missing key, a wrong JSON type, or an array
  //                           whose length disagrees with the counts the
  //                           document itself declares.
  //   InvalidArgumentError    the schema is right but the content is not a
  //                           valid checkpoint for `mesh`: unsupported format
  //                           version, non-finite value, cell/face/dimension
  //                           or mesh-fingerprint mismatch.
  //
  // Every message names the offending file and field. A bare "invalid
  // checkpoint" is never produced.
  [[nodiscard]] static cfd::pressure_velocity::SIMPLECheckpoint read(
      const std::filesystem::path& path, const cfd::mesh::Mesh& mesh);

  // The case-identity check, kept separate from read() on purpose: `mesh`
  // proves the discretization is the same, `caseFingerprint` proves the
  // PROBLEM is the same (viscosity, boundary values, schemes, relaxation,
  // tolerances). A caller that has a case directory should always call this;
  // a library caller that has only a mesh cannot, and is not forced to.
  //
  // Throws InvalidArgumentError naming both fingerprints if they differ.
  static void requireCaseFingerprint(const cfd::pressure_velocity::SIMPLECheckpoint& checkpoint,
                                     const std::string& caseFingerprint,
                                     const std::filesystem::path& path);
};

}  // namespace cfd::io

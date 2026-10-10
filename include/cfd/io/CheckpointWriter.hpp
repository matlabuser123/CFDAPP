#pragma once

// P13-001 -- Restart / checkpointing: the production checkpoint file writer.
// The only place that knows the on-disk schema; the solver-state model lives
// in cfd::pressure_velocity::SIMPLECheckpoint. Mirrors RestartWriter.hpp's
// own static-method, deterministic-formatting convention exactly.
//
// The schema, and nothing else in it:
//
//   format_version                       number
//   application  { version, backend }    strings, informational only
//   identity     { case_fingerprint, mesh_fingerprint,
//                  cell_count, face_count, dimension }
//   state        { completed_iterations, status }
//   fields       { pressure[], velocity_x[], velocity_y[], velocity_z[],
//                  mass_flux[] }
//   history      { u[], v[], pressure[], continuity[],
//                  global_imbalance[], w[]?, turbulence[]? }
//
// No device pointer, CUDA handle, stream id, allocator state or Krylov
// scratch appears anywhere in it, and none could: the model it serializes
// carries none. `w` is present exactly for a 3D checkpoint; `turbulence` is
// present only when the active model reported a convergence residual.
//
// velocity_z is always written, including in 2D (where it is 0 in every
// cell), because VectorField has carried three components since P12-MESH-005
// and a format that dropped one would have to guess on the way back in.
// This is the one place this format deliberately differs in shape from
// cfd::solver::RestartSnapshot's version-1 schema, which is 2D-only.

#include <filesystem>

#include "cfd/pressure_velocity/SIMPLECheckpoint.hpp"

namespace cfd::io {

class CheckpointWriter {
 public:
  // Writes `checkpoint` to `path` as a single self-contained JSON document
  // with stable 2-space-indented key ordering, overwriting any existing file.
  // Every floating-point value goes through nlohmann's default double
  // serialization, which round-trips exactly -- so a write/read cycle is
  // bitwise, which the restart equivalence gate requires.
  //
  // Re-validates the checkpoint first (validateSIMPLECheckpoint), so an
  // invalid one is never written to disk to be discovered later by a reader.
  //
  // Throws IOError if `path` cannot be opened for writing or if writing
  // fails; throws InvalidArgumentError if the checkpoint is invalid.
  static void write(const std::filesystem::path& path,
                    const cfd::pressure_velocity::SIMPLECheckpoint& checkpoint);
};

}  // namespace cfd::io

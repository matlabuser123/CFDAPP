#pragma once

// Restart-C (TODO.md P2 -- Restart capability): the restart file writer.
// Deliberately separate from RestartSnapshot.hpp (the solver-state
// model, Restart-A/B) -- this file is the only place that knows the
// on-disk JSON schema. Mirrors JSONWriter.hpp's own static-method,
// deterministic-formatting convention exactly.

#include <filesystem>

#include "cfd/solver/RestartSnapshot.hpp"

namespace cfd::io {

class RestartWriter {
 public:
  // Writes `snapshot` to `path` as a single self-contained JSON file --
  // format_version, state (time/step/delta_t), mesh identity
  // (cell_count/face_count/fingerprint), and fields (pressure/
  // velocity_x/velocity_y/mass_flux), each field value written with the
  // same 17-significant-digit precision this project's other
  // deterministic exports use (nlohmann::json's default double
  // serialization already round-trips exactly -- no separate formatting
  // step needed), with stable 2-space-indented key ordering. Overwrites
  // any existing file at `path`.
  //
  // Does not itself re-validate `snapshot` (every RestartSnapshot in
  // existence was already validated at construction -- see
  // RestartSnapshot.hpp) or accept an invalid one silently; a caller
  // that somehow holds an invalid snapshot gets whatever
  // validateRestartSnapshot would have thrown, from wherever that
  // snapshot was built.
  //
  // Throws IOError if `path` cannot be opened for writing.
  static void write(const std::filesystem::path& path,
                    const cfd::solver::RestartSnapshot& snapshot);
};

}  // namespace cfd::io

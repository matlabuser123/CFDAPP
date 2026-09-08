#pragma once

// P1 -- Result Export: deterministic, cell-ordered CSV output. Reads
// already-computed Mesh/SIMPLEResult data only -- never recomputes,
// reinterprets, or renormalizes anything the solver produced (section 3).

#include <filesystem>

#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::io {

class CSVWriter {
 public:
  // Writes one row per cell, in cell-id order 0..N-1 (section 5: never
  // filesystem/container iteration order), with the stable header
  // (section 41):
  //   cell_id,x,y,velocity_x,velocity_y,velocity_magnitude,pressure
  // Cell-center coordinates come from Mesh (section 4: never
  // reconstructed from nx/ny when the mesh already owns geometry).
  // Throws InvalidArgumentError if result.velocity/result.pressure size
  // does not match mesh.numberOfCells(); NumericalError if any exported
  // value is non-finite (section 40 -- the failed-solve export policy
  // deciding *whether* to call this at all for a non-finite result lives
  // in ResultExporter, not here); IOError if `path` cannot be opened.
  static void writeFields(const std::filesystem::path& path, const cfd::mesh::Mesh& mesh,
                          const cfd::pressure_velocity::SIMPLEResult& result);

  // Writes one row per completed SIMPLE iteration, in iteration order
  // 1..N, with the stable header (section 8/41):
  //   iteration,u_residual,v_residual,p_residual,continuity_residual,global_mass_imbalance
  // Uses exactly the four histories SIMPLEResult stored during solve()
  // (section 8: never regenerated after the fact) plus the single final
  // globalMassImbalance repeated on every row (SIMPLEResult only stores
  // one, not a per-iteration history -- see SIMPLEResult.hpp). Throws
  // InvalidArgumentError if the four histories' sizes disagree (section
  // 9: reject rather than silently truncate); IOError if `path` cannot
  // be opened.
  static void writeResiduals(const std::filesystem::path& path,
                             const cfd::pressure_velocity::SIMPLEResult& result);
};

}  // namespace cfd::io

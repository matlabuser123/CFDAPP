#pragma once

// P1 -- Result Export: deterministic, cell-ordered CSV output. Reads
// already-computed Mesh/SIMPLEResult data only -- never recomputes,
// reinterprets, or renormalizes anything the solver produced (section 3).

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::io {

// P6-PHYS-001: one named scalar field to append as a trailing CSV column
// / VTK SCALARS block, e.g. {"CO2", concentrationField} -- generalizes
// temperature's single optional field to the N independent
// physics.json-declared species this codebase's field-per-species
// convention produces (see SpeciesProperties.hpp's own header comment).
using NamedScalarField = std::pair<std::string, cfd::fields::ScalarField>;

class CSVWriter {
 public:
  // Writes one row per cell, in cell-id order 0..N-1 (section 5: never
  // filesystem/container iteration order), with the stable header
  // (section 41):
  //   cell_id,x,y,velocity_x,velocity_y,velocity_magnitude,pressure
  // plus a trailing ",temperature" column iff `temperature` is present
  // (P2-THERMAL-004) -- omitted entirely for a nonthermal export, so
  // every existing nonthermal fields.csv (and every reader of it) is
  // unaffected. Cell-center coordinates come from Mesh (section 4: never
  // reconstructed from nx/ny when the mesh already owns geometry).
  //
  // `species` (P6-PHYS-001): appends one further trailing
  // ",concentration_<name>" column per entry, in the given order, after
  // the optional temperature column -- omitted entirely (no columns at
  // all) for an empty `species`, so every existing nonspecies fields.csv
  // is unaffected.
  //
  // Throws InvalidArgumentError if result.velocity/result.pressure size
  // (or temperature's/any species field's, when present) does not match
  // mesh.numberOfCells(); NumericalError if any exported value is
  // non-finite (section 40 -- the failed-solve export policy deciding
  // *whether* to call this at all for a non-finite result lives in
  // ResultExporter, not here); IOError if `path` cannot be opened.
  static void writeFields(const std::filesystem::path& path, const cfd::mesh::Mesh& mesh,
                          const cfd::pressure_velocity::SIMPLEResult& result,
                          const std::optional<cfd::fields::ScalarField>& temperature = std::nullopt,
                          const std::vector<NamedScalarField>& species = {});

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

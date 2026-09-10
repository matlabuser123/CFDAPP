#pragma once

// P1 -- Result Export: legacy ASCII VTK UNSTRUCTURED_GRID output, so the
// solution can be inspected directly in ParaView. UNSTRUCTURED_GRID
// (rather than the simpler RECTILINEAR_GRID a purely-Cartesian mesh
// could use) is chosen deliberately (section 18): it is the format that
// will still fit once the mesh system stops being exclusively
// structured-Cartesian, so this exporter does not need rewriting the
// first time that happens.

#include <filesystem>
#include <optional>

#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::io {

class VTKWriter {
 public:
  // Mesh stores no explicit vertices (P0 scope), so point geometry is
  // reconstructed deterministically from the mesh's inferred structured-
  // Cartesian layout (see src/io/StructuredMeshInfo.hpp) -- an export-
  // only representation, not new state added to the numerical mesh
  // (section 23). Fields are written as CELL_DATA (section 20 -- this
  // solver's data is cell-centered; POINT_DATA would misrepresent that),
  // always in the fixed order pressure, velocity, velocity_magnitude
  // (section 24), plus a trailing "SCALARS temperature" block iff
  // `temperature` is present (P2-THERMAL-004) -- omitted entirely for a
  // nonthermal export. 2D coordinates get an explicit z=0 (section 19).
  // Throws InvalidArgumentError if result.velocity/result.pressure size
  // (or temperature's, when present) does not match mesh.numberOfCells(),
  // or if mesh does not fit the structured layout this exporter assumes;
  // NumericalError if any exported value is non-finite; IOError if `path`
  // cannot be opened.
  static void writeSolution(
      const std::filesystem::path& path, const cfd::mesh::Mesh& mesh,
      const cfd::pressure_velocity::SIMPLEResult& result,
      const std::optional<cfd::fields::ScalarField>& temperature = std::nullopt);
};

}  // namespace cfd::io

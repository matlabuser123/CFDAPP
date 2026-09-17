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
#include <string>
#include <utility>
#include <vector>

#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/io/CSVWriter.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::io {

// P12-MESH-005: one named cell-centred vector field (all three components
// are written), the vector counterpart of NamedScalarField.
using NamedVectorField = std::pair<std::string, cfd::fields::VectorField>;

class VTKWriter {
 public:
  // P12-MESH-005: dimension-generic export of cell-centred fields on a mesh
  // with exactly one structured vertex grid (Mesh::structuredGrid()) --
  // a 3D hexahedral mesh (MeshGeometry::createCartesian3D: POINTS = the
  // (nx+1)(ny+1)(nz+1) grid vertices, one VTK_HEXAHEDRON (12) per cell with
  // corners vertex(i,j,k), (i+1,j,k), (i+1,j+1,k), (i,j+1,k), then the same
  // four at k+1) or a single-grid 2D mesh (VTK_QUAD (9), z = 0). Cells in
  // cell-id order; CELL_DATA: one SCALARS block per scalar field, then one
  // 3-component VECTORS block per vector field, each in the given order,
  // written with the deterministic 17-significant-digit format (values
  // round-trip exactly). Throws InvalidArgumentError if the mesh has no
  // single structured grid, a field size is not the cell count, or a field
  // name is empty or contains whitespace, and NumericalError for a
  // non-finite value -- both before the file is opened; IOError if `path`
  // cannot be opened. writeSolution below remains the 2D SIMPLE-result
  // writer.
  static void writeCellFields(const std::filesystem::path& path, const cfd::mesh::Mesh& mesh,
                              const std::vector<NamedScalarField>& scalarFields,
                              const std::vector<NamedVectorField>& vectorFields = {});

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
  //
  // `extraFields` (P6-PHYS-001, generalized by P6-PHYS-002/003): appends
  // one further "SCALARS" block per entry, named exactly `entry.first`
  // (no prefix added here -- see NamedScalarField's own header comment),
  // in the given order, after the optional temperature block -- omitted
  // entirely (no blocks at all) for an empty `extraFields`, so every case
  // with no such fields produces a byte-identical solution.vtk to before
  // this parameter existed. Reuses CSVWriter.hpp's NamedScalarField (a
  // plain {name, field} pair) rather than a second, VTK-only alias.
  //
  // Throws InvalidArgumentError if result.velocity/result.pressure size
  // (or temperature's/any extraFields entry's, when present) does not
  // match mesh.numberOfCells(), or if mesh does not fit the structured
  // layout this exporter assumes; NumericalError if any exported value
  // is non-finite; IOError if `path` cannot be opened. Two-dimensional
  // meshes only (InvalidArgumentError for a 3D mesh; P12-MESH-005).
  static void writeSolution(
      const std::filesystem::path& path, const cfd::mesh::Mesh& mesh,
      const cfd::pressure_velocity::SIMPLEResult& result,
      const std::optional<cfd::fields::ScalarField>& temperature = std::nullopt,
      const std::vector<NamedScalarField>& extraFields = {});
};

}  // namespace cfd::io

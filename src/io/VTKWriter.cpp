#include "cfd/io/VTKWriter.hpp"

#include <cmath>

#include "DeterministicOstream.hpp"
#include "StructuredMeshInfo.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::io {

using cfd::Index;
using cfd::Real;
using cfd::mesh::Mesh;
using cfd::pressure_velocity::SIMPLEResult;

namespace {

constexpr int kVtkQuadCellType = 9;  // VTK_QUAD.

// Point index for structured grid-vertex (i, j), i in [0, nx], j in [0, ny]
// -- row-major, same canonical convention as cell indexing (section 24:
// fixed, deterministic ordering).
Index pointIndex(Index i, Index j, Index nx) { return (j * (nx + 1)) + i; }

}  // namespace

void VTKWriter::writeSolution(const std::filesystem::path& path, const Mesh& mesh,
                              const SIMPLEResult& result) {
  const Index n = mesh.numberOfCells();
  if (result.velocity.size() != n || result.pressure.size() != n) {
    throw InvalidArgumentError(
        "VTKWriter::writeSolution: result field size does not match mesh cell count");
  }
  for (Index id = 0; id < n; ++id) {
    if (!std::isfinite(result.velocity[id].x) || !std::isfinite(result.velocity[id].y) ||
        !std::isfinite(result.pressure[id])) {
      throw NumericalError("VTKWriter::writeSolution: non-finite value at cell " +
                           std::to_string(id));
    }
  }

  const auto structured = detail::inferStructuredMeshInfo(mesh);
  const Index nx = structured.nx;
  const Index ny = structured.ny;
  const Real dx = structured.dx;
  const Real dy = structured.dy;
  const Index numberOfPoints = (nx + 1) * (ny + 1);

  auto out = detail::openDeterministicOutput(path);

  out << "# vtk DataFile Version 3.0\n"
      << "CFDApp solution\n"
      << "ASCII\n"
      << "DATASET UNSTRUCTURED_GRID\n";

  // --- Points: (nx+1)*(ny+1) grid vertices, z=0 (section 19) ------------
  out << "POINTS " << numberOfPoints << " double\n";
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      out << (static_cast<Real>(i) * dx) << ' ' << (static_cast<Real>(j) * dy) << " 0\n";
    }
  }

  // --- Cells: one quad per mesh cell, counter-clockwise corner order ----
  out << "CELLS " << n << ' ' << (n * 5) << '\n';
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Index p00 = pointIndex(i, j, nx);
      const Index p10 = pointIndex(i + 1, j, nx);
      const Index p11 = pointIndex(i + 1, j + 1, nx);
      const Index p01 = pointIndex(i, j + 1, nx);
      out << "4 " << p00 << ' ' << p10 << ' ' << p11 << ' ' << p01 << '\n';
    }
  }

  out << "CELL_TYPES " << n << '\n';
  for (Index id = 0; id < n; ++id) out << kVtkQuadCellType << '\n';

  // --- Cell data: pressure, velocity, velocity_magnitude, in that fixed
  // order (section 24), cell-id order throughout (matches CELLS above,
  // since cell id = j*nx + i is exactly the loop order used there too).
  out << "CELL_DATA " << n << '\n';

  out << "SCALARS pressure double 1\nLOOKUP_TABLE default\n";
  for (Index id = 0; id < n; ++id) out << result.pressure[id] << '\n';

  out << "VECTORS velocity double\n";
  for (Index id = 0; id < n; ++id) {
    out << result.velocity[id].x << ' ' << result.velocity[id].y << " 0\n";
  }

  out << "SCALARS velocity_magnitude double 1\nLOOKUP_TABLE default\n";
  for (Index id = 0; id < n; ++id) out << magnitude(result.velocity[id]) << '\n';
}

}  // namespace cfd::io

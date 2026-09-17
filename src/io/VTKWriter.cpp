#include "cfd/io/VTKWriter.hpp"

#include <cmath>

#include "DeterministicOstream.hpp"
#include "StructuredMeshInfo.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/StructuredGrid.hpp"

namespace cfd::io {

using cfd::Index;
using cfd::Real;
using cfd::mesh::Mesh;
using cfd::pressure_velocity::SIMPLEResult;

namespace {

constexpr int kVtkQuadCellType = 9;         // VTK_QUAD.
constexpr int kVtkHexahedronCellType = 12;  // VTK_HEXAHEDRON.

// Point index for structured grid-vertex (i, j), i in [0, nx], j in [0, ny]
// -- row-major, same canonical convention as cell indexing (section 24:
// fixed, deterministic ordering).
Index pointIndex(Index i, Index j, Index nx) { return (j * (nx + 1)) + i; }

}  // namespace

void VTKWriter::writeSolution(const std::filesystem::path& path, const Mesh& mesh,
                              const SIMPLEResult& result,
                              const std::optional<cfd::fields::ScalarField>& temperature,
                              const std::vector<NamedScalarField>& extraFields) {
  cfd::mesh::requireTwoDimensional(mesh, "VTKWriter::writeSolution");  // P12-MESH-005
  const Index n = mesh.numberOfCells();
  if (result.velocity.size() != n || result.pressure.size() != n) {
    throw InvalidArgumentError(
        "VTKWriter::writeSolution: result field size does not match mesh cell count");
  }
  if (temperature.has_value() && temperature->size() != n) {
    throw InvalidArgumentError(
        "VTKWriter::writeSolution: temperature size does not match mesh cell count");
  }
  for (const auto& [name, field] : extraFields) {
    if (field.size() != n) {
      throw InvalidArgumentError("VTKWriter::writeSolution: field \"" + name +
                                 "\" size does not match mesh cell count");
    }
  }
  for (Index id = 0; id < n; ++id) {
    if (!std::isfinite(result.velocity[id].x) || !std::isfinite(result.velocity[id].y) ||
        !std::isfinite(result.pressure[id])) {
      throw NumericalError("VTKWriter::writeSolution: non-finite value at cell " +
                           std::to_string(id));
    }
    if (temperature.has_value() && !std::isfinite((*temperature)[id])) {
      throw NumericalError("VTKWriter::writeSolution: non-finite temperature at cell " +
                           std::to_string(id));
    }
    for (const auto& [name, field] : extraFields) {
      if (!std::isfinite(field[id])) {
        throw NumericalError("VTKWriter::writeSolution: non-finite field \"" + name +
                             "\" value at cell " + std::to_string(id));
      }
    }
  }

  const auto& blocks = mesh.structuredBlocks();
  const bool multiBlock = blocks.size() > 1;
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

  if (multiBlock) {
    // P12-MESH-003: every block's vertex grid in block order (vertices on an
    // interface appear once per block -- a valid, if not welded, VTK grid),
    // then one counter-clockwise quad per cell in cell-id order (block by
    // block, local j * nx + i).
    Index totalPoints = 0;
    for (const auto& block : blocks) totalPoints += (block.nx + 1) * (block.ny + 1);
    out << "POINTS " << totalPoints << " double\n";
    for (const auto& block : blocks) {
      for (const auto& v : block.vertices) out << v.x << ' ' << v.y << " 0\n";
    }
    out << "CELLS " << n << ' ' << (n * 5) << '\n';
    Index offset = 0;
    for (const auto& block : blocks) {
      for (Index j = 0; j < block.ny; ++j) {
        for (Index i = 0; i < block.nx; ++i) {
          out << "4 " << (offset + pointIndex(i, j, block.nx)) << ' '
              << (offset + pointIndex(i + 1, j, block.nx)) << ' '
              << (offset + pointIndex(i + 1, j + 1, block.nx)) << ' '
              << (offset + pointIndex(i, j + 1, block.nx)) << '\n';
        }
      }
      offset += (block.nx + 1) * (block.ny + 1);
    }
  } else {
    // --- Points: (nx+1)*(ny+1) grid vertices, z=0 (section 19) ------------
    // P12-MESH-001: the mesh's own vertex grid when it has one (every
    // production mesh -- for a Cartesian mesh vertex(i,j) = (i*dx, j*dy), the
    // same values as the reconstruction below); reconstructed from the
    // Cartesian spacing only for a mesh without a grid.
    out << "POINTS " << numberOfPoints << " double\n";
    const cfd::mesh::StructuredGrid* grid = mesh.structuredGrid();
    for (Index j = 0; j <= ny; ++j) {
      for (Index i = 0; i <= nx; ++i) {
        if (grid != nullptr) {
          const Vector2& v = grid->vertex(i, j);
          out << v.x << ' ' << v.y << " 0\n";
        } else {
          out << (static_cast<Real>(i) * dx) << ' ' << (static_cast<Real>(j) * dy) << " 0\n";
        }
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
  }  // single grid

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

  if (temperature.has_value()) {
    out << "SCALARS temperature double 1\nLOOKUP_TABLE default\n";
    for (Index id = 0; id < n; ++id) out << (*temperature)[id] << '\n';
  }

  // The caller supplies each entry's full SCALARS name already -- see
  // CSVWriter.cpp's own comment on why no prefix is added here.
  for (const auto& [name, field] : extraFields) {
    out << "SCALARS " << name << " double 1\nLOOKUP_TABLE default\n";
    for (Index id = 0; id < n; ++id) out << field[id] << '\n';
  }
}

namespace {

void validateFieldName(const std::string& name) {
  if (name.empty() || name.find_first_of(" \t\r\n") != std::string::npos) {
    throw InvalidArgumentError("VTKWriter::writeCellFields: field name \"" + name +
                               "\" must be non-empty and contain no whitespace");
  }
}

}  // namespace

void VTKWriter::writeCellFields(const std::filesystem::path& path, const Mesh& mesh,
                                const std::vector<NamedScalarField>& scalarFields,
                                const std::vector<NamedVectorField>& vectorFields) {
  const cfd::mesh::StructuredGrid* grid = mesh.structuredGrid();
  if (grid == nullptr) {
    throw InvalidArgumentError(
        "VTKWriter::writeCellFields: the mesh has no single structured vertex grid");
  }
  const Index n = mesh.numberOfCells();
  for (const auto& [name, field] : scalarFields) {
    validateFieldName(name);
    if (field.size() != n) {
      throw InvalidArgumentError("VTKWriter::writeCellFields: field \"" + name +
                                 "\" size does not match mesh cell count");
    }
    for (Index id = 0; id < n; ++id) {
      if (!std::isfinite(field[id])) {
        throw NumericalError("VTKWriter::writeCellFields: non-finite field \"" + name +
                             "\" value at cell " + std::to_string(id));
      }
    }
  }
  for (const auto& [name, field] : vectorFields) {
    validateFieldName(name);
    if (field.size() != n) {
      throw InvalidArgumentError("VTKWriter::writeCellFields: field \"" + name +
                                 "\" size does not match mesh cell count");
    }
    for (Index id = 0; id < n; ++id) {
      if (!isFinite(field[id])) {
        throw NumericalError("VTKWriter::writeCellFields: non-finite field \"" + name +
                             "\" value at cell " + std::to_string(id));
      }
    }
  }

  const bool hexahedra = grid->isThreeDimensional();
  const Index nx = grid->nx;
  const Index ny = grid->ny;
  const Index nz = grid->nz;
  const auto point = [nx, ny](Index i, Index j, Index k) -> Index {
    return (((k * (ny + 1)) + j) * (nx + 1)) + i;
  };

  auto out = detail::openDeterministicOutput(path);
  out << "# vtk DataFile Version 3.0\n"
      << "CFDApp cell fields\n"
      << "ASCII\n"
      << "DATASET UNSTRUCTURED_GRID\n";

  out << "POINTS " << grid->vertices.size() << " double\n";
  for (const auto& v : grid->vertices) out << v.x << ' ' << v.y << ' ' << v.z << '\n';

  const Index pointsPerCell = hexahedra ? 8 : 4;
  out << "CELLS " << n << ' ' << (n * (pointsPerCell + 1)) << '\n';
  if (hexahedra) {
    for (Index k = 0; k < nz; ++k) {
      for (Index j = 0; j < ny; ++j) {
        for (Index i = 0; i < nx; ++i) {
          out << "8 " << point(i, j, k) << ' ' << point(i + 1, j, k) << ' '
              << point(i + 1, j + 1, k) << ' ' << point(i, j + 1, k) << ' ' << point(i, j, k + 1)
              << ' ' << point(i + 1, j, k + 1) << ' ' << point(i + 1, j + 1, k + 1) << ' '
              << point(i, j + 1, k + 1) << '\n';
        }
      }
    }
  } else {
    for (Index j = 0; j < ny; ++j) {
      for (Index i = 0; i < nx; ++i) {
        out << "4 " << pointIndex(i, j, nx) << ' ' << pointIndex(i + 1, j, nx) << ' '
            << pointIndex(i + 1, j + 1, nx) << ' ' << pointIndex(i, j + 1, nx) << '\n';
      }
    }
  }
  out << "CELL_TYPES " << n << '\n';
  const int cellType = hexahedra ? kVtkHexahedronCellType : kVtkQuadCellType;
  for (Index id = 0; id < n; ++id) out << cellType << '\n';

  out << "CELL_DATA " << n << '\n';
  for (const auto& [name, field] : scalarFields) {
    out << "SCALARS " << name << " double 1\nLOOKUP_TABLE default\n";
    for (Index id = 0; id < n; ++id) out << field[id] << '\n';
  }
  for (const auto& [name, field] : vectorFields) {
    out << "VECTORS " << name << " double\n";
    for (Index id = 0; id < n; ++id) {
      out << field[id].x << ' ' << field[id].y << ' ' << field[id].z << '\n';
    }
  }
}

}  // namespace cfd::io

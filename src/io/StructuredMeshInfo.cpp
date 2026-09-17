#include "StructuredMeshInfo.hpp"

#include <algorithm>
#include <cmath>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/StructuredGrid.hpp"

namespace cfd::io::detail {

using cfd::Index;
using cfd::mesh::Mesh;

StructuredMeshInfo inferStructuredMeshInfo(const Mesh& mesh) {
  cfd::mesh::requireTwoDimensional(mesh, "inferStructuredMeshInfo (2D export layout)");
  // P12-MESH-003: a multi-block mesh is not one nx x ny grid.
  if (mesh.structuredBlocks().size() > 1) return StructuredMeshInfo{0, 0, 0.0, 0.0};
  if (const cfd::mesh::StructuredGrid* grid = mesh.structuredGrid()) {
    const Real dx = magnitude(grid->vertex(1, 0) - grid->vertex(0, 0));
    const Real dy = magnitude(grid->vertex(0, 1) - grid->vertex(0, 0));
    return StructuredMeshInfo{grid->nx, grid->ny, dx, dy};
  }
  const Index numberOfCells = mesh.numberOfCells();
  if (numberOfCells == 0) {
    throw InvalidArgumentError("inferStructuredMeshInfo: mesh has no cells");
  }

  // Row length: how many consecutive cells (starting at cell 0) share
  // cell 0's y-centroid -- exactly nx for a row-major
  // createCartesian2D mesh.
  const Real firstRowY = mesh.cell(0).centroid().y;
  Index nx = 0;
  while (nx < numberOfCells && mesh.cell(nx).centroid().y == firstRowY) ++nx;

  if (nx == 0 || numberOfCells % nx != 0) {
    throw InvalidArgumentError(
        "inferStructuredMeshInfo: mesh does not fit the structured Cartesian (row-major, uniform "
        "row length) layout this export representation assumes");
  }
  const Index ny = numberOfCells / nx;

  // cell(0,0) centroid = (0.5*dx, 0.5*dy) (MeshGeometry::createCartesian2D,
  // domain origin at (0,0)).
  const Real dx = 2.0 * mesh.cell(0).centroid().x;
  const Real dy = 2.0 * mesh.cell(0).centroid().y;

  // P12-MESH-001: verify the whole layout, not just the first row.
  const Real tolerance = 1e-9 * std::max(static_cast<Real>(nx) * dx, static_cast<Real>(ny) * dy);
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const auto& c = mesh.cell((j * nx) + i).centroid();
      if (std::abs(c.x - ((static_cast<Real>(i) + 0.5) * dx)) > tolerance ||
          std::abs(c.y - ((static_cast<Real>(j) + 0.5) * dy)) > tolerance) {
        throw InvalidArgumentError(
            "inferStructuredMeshInfo: mesh has no structured grid and its cell centroids do not "
            "fit the uniform structured Cartesian layout this export representation assumes");
      }
    }
  }

  return StructuredMeshInfo{nx, ny, dx, dy};
}

}  // namespace cfd::io::detail

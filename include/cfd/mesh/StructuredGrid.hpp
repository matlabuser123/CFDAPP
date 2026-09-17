#pragma once

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::mesh {

// P12-MESH-001: the logical vertex grid of a structured 2D quadrilateral
// mesh -- (nx + 1) x (ny + 1) vertices, row-major:
//   vertex(i, j) = vertices[j * (nx + 1) + i],  0 <= i <= nx, 0 <= j <= ny,
// cell(i, j) = j * nx + i has the counter-clockwise corners vertex(i, j),
// vertex(i+1, j), vertex(i+1, j+1), vertex(i, j+1).
//
// This is the generating geometry of a mesh built by
// MeshGeometry::createCartesian2D or MeshGeometry::createStructuredQuad2D:
// every cell centroid/volume and face centroid/area vector of such a mesh
// is derived from it. The numerical operators never read it (they use the
// derived cell/face geometry only); it is kept on the Mesh so exporters can
// write the true cell polygons instead of reconstructing a Cartesian grid.
//
// P12-MESH-003: a multi-block mesh keeps one StructuredGrid per block
// (Mesh::structuredBlocks(), in block order; block b's cells are the
// contiguous id range starting at the sum of the earlier blocks' nx * ny,
// with local cell(i, j) = offset + j * nx + i). `name` is the block's case
// name (empty for single-grid meshes).
//
// P12-MESH-005: nz > 0 makes it the vertex grid of a 3D hexahedral mesh
// (MeshGeometry::createCartesian3D) -- (nx + 1) x (ny + 1) x (nz + 1)
// vertices,
//   vertex(i, j, k) = vertices[(k * (ny + 1) + j) * (nx + 1) + i],
// cell(i, j, k) = (k * ny + j) * nx + i with the corners vertex(i, j, k),
// (i+1, j, k), (i+1, j+1, k), (i, j+1, k) and the same four at k + 1.
// nz = 0 (the default) is a 2D grid, exactly as before.
struct StructuredGrid {
  Index nx{0};
  Index ny{0};
  std::vector<Vector2> vertices;
  std::string name{};
  Index nz{0};

  [[nodiscard]] bool isThreeDimensional() const noexcept { return nz > 0; }

  // nx * ny (2D) or nx * ny * nz (3D).
  [[nodiscard]] Index cellCount() const noexcept {
    return isThreeDimensional() ? nx * ny * nz : nx * ny;
  }

  // (nx + 1)(ny + 1) (2D) or (nx + 1)(ny + 1)(nz + 1) (3D).
  [[nodiscard]] Index vertexCount() const noexcept {
    return isThreeDimensional() ? (nx + 1) * (ny + 1) * (nz + 1) : (nx + 1) * (ny + 1);
  }

  [[nodiscard]] const Vector2& vertex(Index i, Index j) const {
    return vertices[(j * (nx + 1)) + i];
  }

  [[nodiscard]] const Vector3& vertex(Index i, Index j, Index k) const {
    return vertices[(((k * (ny + 1)) + j) * (nx + 1)) + i];
  }
};

}  // namespace cfd::mesh

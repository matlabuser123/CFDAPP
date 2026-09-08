#pragma once

#include <optional>

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::mesh {

// Geometric computations derived from mesh data, plus the structured
// Cartesian mesh generator. Does not store a duplicate mesh -- every
// method is a pure function of the Mesh/Face/Vector2 arguments passed in.
class MeshGeometry {
 public:
  MeshGeometry() = delete;

  [[nodiscard]] static Real distance(const Vector2& a, const Vector2& b) noexcept;
  [[nodiscard]] static Vector2 displacement(const Vector2& from, const Vector2& to) noexcept;

  [[nodiscard]] static Vector2 unitNormal(const Face& face);
  [[nodiscard]] static Real ownerNeighborDistance(const Mesh& mesh, const Face& face);

  // Among `cell`'s other faces, finds the internal face whose outward-
  // from-`cell` unit normal is most anti-parallel to `boundaryFace`'s
  // outward normal -- i.e. the face "across" the cell from a boundary
  // face, found by topology/geometry rather than any structured-mesh
  // indexing. Returns std::nullopt if the cell has no internal face at
  // all (e.g. every side of `cell` is a boundary, as in a 1x1 mesh).
  // Used to build a second-order one-sided boundary derivative estimate
  // from three points (boundary, owner, opposite neighbor) instead of
  // the first-order two-point secant.
  [[nodiscard]] static std::optional<Index> oppositeInteriorFace(const Mesh& mesh, const Cell& cell,
                                                                 const Face& boundaryFace);

  // Generates a structured, orthogonal, cell-centered 2D Cartesian mesh
  // over [0, lengthX] x [0, lengthY] with nx * ny cells.
  //
  // Indexing: cell(i, j) = j * nx + i, 0 <= i < nx, 0 <= j < ny.
  // Area vectors: Sf points owner -> neighbor for internal faces, and
  // outward from the domain for boundary faces (see docs/architecture).
  // Boundary patches: "left", "right", "bottom", "top".
  [[nodiscard]] static Mesh createCartesian2D(Index nx, Index ny, Real lengthX, Real lengthY);
};

}  // namespace cfd::mesh

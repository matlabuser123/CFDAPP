#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/StructuredGrid.hpp"

namespace cfd::mesh {

// P12-MESH-007: the geometry of a mesh -- everything a vertex motion changes
// -- without its topology: cell centroids and volumes, face centroids and
// area vectors (cell-id / face-id order), and the vertex coordinates of each
// structured block (Mesh::structuredBlocks() order, each block's own vertex
// order). See results/p12-mesh-007/architecture.md section 3.
struct MeshGeometryState {
  std::vector<Vector3> cellCentroids;
  std::vector<Real> cellVolumes;
  std::vector<Vector3> faceCentroids;
  std::vector<Vector3> faceAreaVectors;
  std::vector<std::vector<Vector3>> blockVertices;
};

// Owns the complete mesh: cells, faces, and boundary patches. Cells store
// face IDs and faces store owner/neighbor cell IDs; neither duplicates the
// other's full objects (see PROJECT_STRUCTURE.md). Cell/face ids are their
// position in cells()/faces(), assigned by whatever built the Mesh (e.g.
// MeshGeometry::createCartesian2D).
class Mesh {
 public:
  Mesh(std::vector<Cell> cells, std::vector<Face> faces,
       std::vector<BoundaryPatch> boundaryPatches);
  // P12-MESH-001: a mesh built from a structured vertex grid (see
  // StructuredGrid.hpp) keeps that grid. Throws InvalidArgumentError if the
  // grid does not match the mesh (nx * ny != cell count, or a vertex count
  // other than (nx + 1) * (ny + 1)).
  Mesh(std::vector<Cell> cells, std::vector<Face> faces, std::vector<BoundaryPatch> boundaryPatches,
       StructuredGrid structuredGrid);
  // P12-MESH-003: a multi-block mesh keeps one grid per block, in cell-id
  // order (see StructuredGrid.hpp). Throws InvalidArgumentError if the
  // blocks' nx * ny do not add up to the cell count or a block's vertex
  // count is not (nx + 1) * (ny + 1). (P12-MESH-005: for a 3D grid, nz > 0,
  // nx * ny * nz and (nx + 1)(ny + 1)(nz + 1); a grid's dimension must be
  // the mesh's.)
  Mesh(std::vector<Cell> cells, std::vector<Face> faces, std::vector<BoundaryPatch> boundaryPatches,
       std::vector<StructuredGrid> structuredBlocks);

  [[nodiscard]] const std::vector<Cell>& cells() const noexcept;
  [[nodiscard]] const std::vector<Face>& faces() const noexcept;
  [[nodiscard]] const std::vector<BoundaryPatch>& boundaryPatches() const noexcept;

  [[nodiscard]] std::size_t numberOfCells() const noexcept;
  [[nodiscard]] std::size_t numberOfFaces() const noexcept;

  [[nodiscard]] const Cell& cell(Index id) const;
  [[nodiscard]] const Face& face(Index id) const;

  [[nodiscard]] const BoundaryPatch& boundaryPatch(std::string_view name) const;

  // The generating vertex grid of a single-grid mesh, or nullptr for a mesh
  // assembled directly from cells/faces (e.g. a re-patched copy) or built
  // from several blocks. Never used by the numerics.
  [[nodiscard]] const StructuredGrid* structuredGrid() const noexcept;

  // P12-MESH-003: every generating block grid, in cell-id order (one entry
  // for a single-grid mesh, empty for a mesh without grids). Export only.
  [[nodiscard]] const std::vector<StructuredGrid>& structuredBlocks() const noexcept;

  // P12-MESH-005: the spatial dimension, 2 or 3, derived from the geometry
  // when the mesh is constructed: 3 if any face area vector has a non-zero z
  // component, otherwise 2. A 2D mesh (every face normal in the xy-plane) is
  // 2; a 3D mesh always has faces with a z-directed normal (a bounded cell's
  // outward normals cannot all lie in one plane), so it is 3.
  [[nodiscard]] int dimension() const noexcept;

  // P12-MESH-007: the current geometry (a copy) -- see MeshGeometryState.
  [[nodiscard]] MeshGeometryState geometry() const;

  // P12-MESH-007: replaces the geometry, never the topology (cell and face
  // ids, owners/neighbors, cell face lists, patches and block sizes have no
  // setter at all, so a geometry update cannot change them). The only way a
  // mesh's geometry changes after construction; used by MeshMotion.
  //
  // Validates everything before changing anything (strong guarantee) and
  // throws InvalidArgumentError, naming the offending cell / face / block, if:
  // a size differs from the mesh's (cells, faces, blocks, a block's vertex
  // count); a value is not finite; a cell volume is not positive; a face area
  // is not positive; or a two-dimensional mesh would leave the xy-plane (a
  // centroid, vertex or area vector with a non-zero z component). The
  // dimension never changes.
  void setGeometry(const MeshGeometryState& geometry);

 private:
  std::vector<Cell> cells_;
  std::vector<Face> faces_;
  std::vector<BoundaryPatch> boundaryPatches_;
  std::vector<StructuredGrid> structuredBlocks_;
  int dimension_{2};
};

// P12-MESH-005: throws InvalidArgumentError naming `component` if `mesh` is
// three-dimensional. Called at the entry of every component that is still
// two-dimensional by construction (momentum (u, v), pressure correction,
// velocity gradient, wall distance, vorticity, the 2D solution writers,
// restart) so that a 3D mesh is refused explicitly instead of silently
// producing a result that ignores z. 3D incompressible flow is P12-MESH-006.
void requireTwoDimensional(const Mesh& mesh, std::string_view component);

}  // namespace cfd::mesh

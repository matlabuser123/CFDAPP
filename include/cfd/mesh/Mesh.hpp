#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"

namespace cfd::mesh {

// Owns the complete mesh: cells, faces, and boundary patches. Cells store
// face IDs and faces store owner/neighbor cell IDs; neither duplicates the
// other's full objects (see PROJECT_STRUCTURE.md). Cell/face ids are their
// position in cells()/faces(), assigned by whatever built the Mesh (e.g.
// MeshGeometry::createCartesian2D).
class Mesh {
 public:
  Mesh(std::vector<Cell> cells, std::vector<Face> faces,
       std::vector<BoundaryPatch> boundaryPatches);

  [[nodiscard]] const std::vector<Cell>& cells() const noexcept;
  [[nodiscard]] const std::vector<Face>& faces() const noexcept;
  [[nodiscard]] const std::vector<BoundaryPatch>& boundaryPatches() const noexcept;

  [[nodiscard]] std::size_t numberOfCells() const noexcept;
  [[nodiscard]] std::size_t numberOfFaces() const noexcept;

  [[nodiscard]] const Cell& cell(Index id) const;
  [[nodiscard]] const Face& face(Index id) const;

  [[nodiscard]] const BoundaryPatch& boundaryPatch(std::string_view name) const;

 private:
  std::vector<Cell> cells_;
  std::vector<Face> faces_;
  std::vector<BoundaryPatch> boundaryPatches_;
};

}  // namespace cfd::mesh

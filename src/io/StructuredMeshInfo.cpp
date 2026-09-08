#include "StructuredMeshInfo.hpp"

#include "cfd/core/Exception.hpp"

namespace cfd::io::detail {

using cfd::Index;
using cfd::mesh::Mesh;

StructuredMeshInfo inferStructuredMeshInfo(const Mesh& mesh) {
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

  return StructuredMeshInfo{nx, ny, dx, dy};
}

}  // namespace cfd::io::detail

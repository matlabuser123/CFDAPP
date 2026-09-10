#include "cfd/turbulence/WallDistance.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::turbulence {

using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::BoundaryConditionType;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

ScalarField computeWallDistance(const Mesh& mesh, const BoundaryConditionSet& velocityBoundaries) {
  std::vector<Index> wallFaceIds;
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto& bc = velocityBoundaries.get(patch.name());
    if (bc.type() == BoundaryConditionType::Wall ||
        bc.type() == BoundaryConditionType::MovingWall) {
      for (const Index faceId : patch.faceIds()) {
        wallFaceIds.push_back(faceId);
      }
    }
  }
  if (wallFaceIds.empty()) {
    throw InvalidArgumentError(
        "computeWallDistance: velocityBoundaries has no Wall/MovingWall patch");
  }

  const Index n = mesh.numberOfCells();
  ScalarField distance(n);
  for (const auto& cell : mesh.cells()) {
    Real minDistance = std::numeric_limits<Real>::infinity();
    for (const Index faceId : wallFaceIds) {
      const Real d = MeshGeometry::distance(cell.centroid(), mesh.face(faceId).centroid());
      minDistance = std::min(minDistance, d);
    }
    if (!std::isfinite(minDistance) || !(minDistance > 0.0)) {
      throw InvalidArgumentError(
          "computeWallDistance: computed a non-finite or non-positive "
          "distance for cell " +
          std::to_string(cell.id()));
    }
    distance[cell.id()] = minDistance;
  }
  return distance;
}

}  // namespace cfd::turbulence

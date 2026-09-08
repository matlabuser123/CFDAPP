#include "cfd/discretization/Divergence.hpp"

#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

ScalarField divergence(const Mesh& mesh, const VectorField& field,
                       const BoundaryConditionSet& boundaries) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("divergence: field size does not match mesh cell count");
  }

  // Each face's interpolated vector value is computed once here (not
  // recomputed per adjacent cell), so internal-face contributions cancel
  // exactly when summed over the whole mesh.
  std::vector<Vector2> faceValues(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    faceValues[faceId] = interpolateFace(mesh, mesh.face(faceId), field, boundaries);
  }

  ScalarField result(mesh.numberOfCells(), 0.0);
  for (const auto& cell : mesh.cells()) {
    Real sum = 0.0;
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Vector2 sfCell =
          (face.owner() == cell.id()) ? face.areaVector() : (face.areaVector() * -1.0);
      sum += dot(faceValues[faceId], sfCell);
    }
    result[cell.id()] = sum / cell.volume();
  }
  return result;
}

}  // namespace cfd::discretization

#include "cfd/discretization/VectorGradient.hpp"

#include <utility>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryConditionSet;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;

VelocityGradientField computeVelocityGradient(const Mesh& mesh, const VectorField& velocity,
                                              const BoundaryConditionSet& velocityBoundaries) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "computeVelocityGradient: velocity size does not match mesh cell "
        "count");
  }

  VectorField gradU(mesh.numberOfCells(), Vector2{0.0, 0.0});
  VectorField gradV(mesh.numberOfCells(), Vector2{0.0, 0.0});

  for (const auto& cell : mesh.cells()) {
    Vector2 sumU{0.0, 0.0};
    Vector2 sumV{0.0, 0.0};
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Vector2 faceVelocity = interpolateFace(mesh, face, velocity, velocityBoundaries);
      const Vector2 sfCell =
          (face.owner() == cell.id()) ? face.areaVector() : (face.areaVector() * -1.0);
      sumU += sfCell * faceVelocity.x;
      sumV += sfCell * faceVelocity.y;
    }
    gradU[cell.id()] = sumU * (1.0 / cell.volume());
    gradV[cell.id()] = sumV * (1.0 / cell.volume());
  }

  return VelocityGradientField{std::move(gradU), std::move(gradV)};
}

}  // namespace cfd::discretization

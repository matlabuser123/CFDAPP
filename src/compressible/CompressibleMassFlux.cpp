#include "cfd/compressible/CompressibleMassFlux.hpp"

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"

namespace cfd::compressible {

using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;

SurfaceField calculateCompressibleMassFlux(const Mesh& mesh, const VectorField& velocity,
                                           const ScalarField& density,
                                           const BoundaryConditionSet& velocityBoundaries) {
  if (velocity.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "calculateCompressibleMassFlux: velocity size does not match mesh cell count");
  }
  if (density.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "calculateCompressibleMassFlux: density size does not match mesh cell count");
  }

  SurfaceField massFlux(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Face& face = mesh.face(faceId);
    const Vector2 faceVelocity =
        cfd::discretization::interpolateFace(mesh, face, velocity, velocityBoundaries);
    const Real faceDensity = face.isBoundary()
                                 ? density[face.owner()]
                                 : cfd::discretization::interpolateInternalFace(mesh, face, density);
    massFlux[faceId] = faceDensity * dot(faceVelocity, face.areaVector());
  }
  return massFlux;
}

}  // namespace cfd::compressible

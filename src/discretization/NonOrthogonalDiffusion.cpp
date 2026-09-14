#include "cfd/discretization/NonOrthogonalDiffusion.hpp"

#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryConditionType;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

bool prescribesBoundaryValue(BoundaryConditionType type) noexcept {
  switch (type) {
    case BoundaryConditionType::FixedValue:
    case BoundaryConditionType::FixedTemperature:
    case BoundaryConditionType::WallOmega:
    case BoundaryConditionType::Wall:
    case BoundaryConditionType::MovingWall:
    case BoundaryConditionType::Inlet:
      return true;
    case BoundaryConditionType::FixedGradient:
    case BoundaryConditionType::HeatFlux:
    case BoundaryConditionType::Adiabatic:
    case BoundaryConditionType::Outlet:
    case BoundaryConditionType::Symmetry:
      return false;
  }
  return false;
}

FaceDiffusionTerms internalFaceDiffusionTerms(const Mesh& mesh, const Face& face, Real gammaFace,
                                              Real distance, const VectorField* gradPhi) {
  if (gradPhi != nullptr) {
    const auto decomposition = MeshGeometry::decomposeFaceArea(mesh, face);
    if (decomposition.valid) {
      const Vector2 gradFace = interpolateInternalFace(mesh, face, *gradPhi);
      return FaceDiffusionTerms{gammaFace * magnitude(decomposition.orthogonal) / distance,
                                gammaFace * dot(decomposition.nonOrthogonal, gradFace)};
    }
  }
  return FaceDiffusionTerms{gammaFace * face.area() / distance, 0.0};
}

FaceDiffusionTerms boundaryFaceDiffusionTerms(const Mesh& mesh, const Face& face, Real gammaFace,
                                              Real distance, const VectorField* gradPhi,
                                              bool prescribedValue) {
  if (gradPhi != nullptr && prescribedValue) {
    const auto decomposition = MeshGeometry::decomposeBoundaryFaceArea(mesh, face);
    if (decomposition.valid) {
      return FaceDiffusionTerms{
          gammaFace * magnitude(decomposition.orthogonal) / distance,
          gammaFace * dot(decomposition.nonOrthogonal, (*gradPhi)[face.owner()])};
    }
  }
  return FaceDiffusionTerms{gammaFace * face.area() / distance, 0.0};
}

}  // namespace cfd::discretization

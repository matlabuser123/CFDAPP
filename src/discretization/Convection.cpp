#include "cfd/discretization/Convection.hpp"

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::ScalarBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

Real upwindInternalFaceValue(const Face& face, const ScalarField& field, Real faceFlux) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("upwindInternalFaceValue: face is a boundary face");
  }
  return (faceFlux >= 0.0) ? field[face.owner()] : field[*face.neighbor()];
}

Real upwindBoundaryFaceValue(const Mesh& mesh, const Face& face, const ScalarField& field,
                             Real faceFlux, const ScalarBoundaryCondition& bc) {
  if (!face.isBoundary()) {
    throw InvalidArgumentError("upwindBoundaryFaceValue: face is not a boundary face");
  }
  if (faceFlux >= 0.0) {
    return field[face.owner()];  // outflow: carries the interior value
  }
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  return bc.boundaryValue(field[face.owner()], distance);  // inflow: carries the boundary value
}

namespace {

Real upwindFaceValue(const Mesh& mesh, const Face& face, const ScalarField& field, Real faceFlux,
                     const BoundaryConditionSet& boundaries) {
  if (!face.isBoundary()) {
    return upwindInternalFaceValue(face, field, faceFlux);
  }
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("convection: boundary condition is not scalar-valued");
  }
  return upwindBoundaryFaceValue(mesh, face, field, faceFlux, *scalarBc);
}

}  // namespace

ScalarField convection(const Mesh& mesh, const ScalarField& field, const SurfaceField& faceMassFlux,
                       const BoundaryConditionSet& boundaries) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("convection: field size does not match mesh cell count");
  }
  if (faceMassFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("convection: faceMassFlux size does not match mesh face count");
  }

  ScalarField result(mesh.numberOfCells(), 0.0);
  for (const auto& cell : mesh.cells()) {
    Real sum = 0.0;
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Real ownerFlux = faceMassFlux[faceId];
      const Real phiUpwind = upwindFaceValue(mesh, face, field, ownerFlux, boundaries);
      const Real cellFlux = (face.owner() == cell.id()) ? ownerFlux : -ownerFlux;
      sum += cellFlux * phiUpwind;
    }
    result[cell.id()] = sum / cell.volume();
  }
  return result;
}

}  // namespace cfd::discretization

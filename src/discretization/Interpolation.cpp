#include "cfd/discretization/Interpolation.hpp"

#include <string>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::ScalarBoundaryCondition;
using cfd::boundary::VectorBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

Real interpolateInternalFace(const Mesh& mesh, const Face& face, const ScalarField& field) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("interpolateInternalFace: face is a boundary face");
  }
  const Real dPf = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const Real dNf = MeshGeometry::distance(mesh.cell(*face.neighbor()).centroid(), face.centroid());
  const Real phiP = field[face.owner()];
  const Real phiN = field[*face.neighbor()];
  return ((dNf * phiP) + (dPf * phiN)) / (dPf + dNf);
}

Vector2 interpolateInternalFace(const Mesh& mesh, const Face& face, const VectorField& field) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("interpolateInternalFace: face is a boundary face");
  }
  const Real dPf = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const Real dNf = MeshGeometry::distance(mesh.cell(*face.neighbor()).centroid(), face.centroid());
  const Vector2& uP = field[face.owner()];
  const Vector2& uN = field[*face.neighbor()];
  return ((uP * dNf) + (uN * dPf)) * (1.0 / (dPf + dNf));
}

Real interpolateInternalFaceSkewCorrected(const Mesh& mesh, const Face& face,
                                          const ScalarField& field, const VectorField& gradient) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("interpolateInternalFaceSkewCorrected: face is a boundary face");
  }
  if (field.size() != mesh.numberOfCells() || gradient.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError(
        "interpolateInternalFaceSkewCorrected: field/gradient size does not match mesh cell count");
  }
  const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, face);
  // Undefined crossing, or an unskewed face (skew vector exactly zero):
  // the plain interpolation, bit-for-bit.
  if (!crossing.has_value() || crossing->skewVector == Vector3{}) {
    return interpolateInternalFace(mesh, face, field);
  }
  const Index ownerId = face.owner();
  const Index neighborId = *face.neighbor();
  const Real t = crossing->t;
  const Real phiCrossing = field[ownerId] + (t * (field[neighborId] - field[ownerId]));
  const Vector2 gradCrossing = gradient[ownerId] + ((gradient[neighborId] - gradient[ownerId]) * t);
  return phiCrossing + dot(gradCrossing, crossing->skewVector);
}

Vector2 interpolateInternalFaceSkewCorrected(const Mesh& mesh, const Face& face,
                                             const VectorField& field, const VectorField& gradientX,
                                             const VectorField& gradientY,
                                             const VectorField* gradientZ) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("interpolateInternalFaceSkewCorrected: face is a boundary face");
  }
  if (field.size() != mesh.numberOfCells() || gradientX.size() != mesh.numberOfCells() ||
      gradientY.size() != mesh.numberOfCells() ||
      (gradientZ != nullptr && gradientZ->size() != mesh.numberOfCells())) {
    throw InvalidArgumentError(
        "interpolateInternalFaceSkewCorrected: field/gradient size does not match mesh cell count");
  }
  // P12-MESH-006: a 3D velocity needs the w gradient too (a 2D one has none).
  if (mesh.dimension() == 3 && gradientZ == nullptr) {
    throw InvalidArgumentError(
        "interpolateInternalFaceSkewCorrected: a 3D velocity field needs gradientZ");
  }
  const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, face);
  if (!crossing.has_value() || crossing->skewVector == Vector3{}) {
    return interpolateInternalFace(mesh, face, field);
  }
  const Index ownerId = face.owner();
  const Index neighborId = *face.neighbor();
  const Real t = crossing->t;
  const Vector2 valueCrossing = field[ownerId] + ((field[neighborId] - field[ownerId]) * t);
  const Vector2 gradXCrossing =
      gradientX[ownerId] + ((gradientX[neighborId] - gradientX[ownerId]) * t);
  const Vector2 gradYCrossing =
      gradientY[ownerId] + ((gradientY[neighborId] - gradientY[ownerId]) * t);
  if (gradientZ == nullptr) {
    return Vector2{valueCrossing.x + dot(gradXCrossing, crossing->skewVector),
                   valueCrossing.y + dot(gradYCrossing, crossing->skewVector)};
  }
  const Vector3 gradZCrossing =
      (*gradientZ)[ownerId] + (((*gradientZ)[neighborId] - (*gradientZ)[ownerId]) * t);
  return Vector3{valueCrossing.x + dot(gradXCrossing, crossing->skewVector),
                 valueCrossing.y + dot(gradYCrossing, crossing->skewVector),
                 valueCrossing.z + dot(gradZCrossing, crossing->skewVector)};
}

Real interpolateBoundaryFace(const Mesh& mesh, const Face& face, const ScalarField& field,
                             const ScalarBoundaryCondition& bc) {
  if (!face.isBoundary()) {
    throw InvalidArgumentError("interpolateBoundaryFace: face is not a boundary face");
  }
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  return bc.boundaryValue(field[face.owner()], distance);
}

Vector2 interpolateBoundaryFace(const Mesh& mesh, const Face& face, const VectorField& field,
                                const VectorBoundaryCondition& bc) {
  if (!face.isBoundary()) {
    throw InvalidArgumentError("interpolateBoundaryFace: face is not a boundary face");
  }
  const Real distance = MeshGeometry::distance(mesh.cell(face.owner()).centroid(), face.centroid());
  const Vector2 unitNormal = MeshGeometry::unitNormal(face);
  return bc.boundaryValue(field[face.owner()], distance, unitNormal);
}

Real interpolateFace(const Mesh& mesh, const Face& face, const ScalarField& field,
                     const BoundaryConditionSet& boundaries) {
  if (!face.isBoundary()) {
    return interpolateInternalFace(mesh, face, field);
  }
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("interpolateFace: boundary condition for face " +
                               std::to_string(face.id()) + " is not scalar-valued");
  }
  return interpolateBoundaryFace(mesh, face, field, *scalarBc);
}

Vector2 interpolateFace(const Mesh& mesh, const Face& face, const VectorField& field,
                        const BoundaryConditionSet& boundaries) {
  if (!face.isBoundary()) {
    return interpolateInternalFace(mesh, face, field);
  }
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
  const auto* vectorBc = dynamic_cast<const VectorBoundaryCondition*>(&bc);
  if (vectorBc == nullptr) {
    throw InvalidArgumentError("interpolateFace: boundary condition for face " +
                               std::to_string(face.id()) + " is not vector-valued");
  }
  return interpolateBoundaryFace(mesh, face, field, *vectorBc);
}

SurfaceField interpolate(const Mesh& mesh, const ScalarField& field,
                         const BoundaryConditionSet& boundaries) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("interpolate: field size does not match mesh cell count");
  }
  SurfaceField result(mesh.numberOfFaces());
  for (Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    result[faceId] = interpolateFace(mesh, mesh.face(faceId), field, boundaries);
  }
  return result;
}

}  // namespace cfd::discretization

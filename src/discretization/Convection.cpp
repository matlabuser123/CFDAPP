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
  const Real phiOwner = field[face.owner()];
  const Real phiBoundary = bc.boundaryValue(phiOwner, distance);

  // inflow: NOT simply phiBoundary. Every *interior* upwind face feeds the
  // scheme an upstream value from a cell one full owner-to-neighbor
  // spacing away -- e.g. an outflow boundary face uses phiOwner itself,
  // representing the flux at a point ~`distance` *past* the owner
  // centroid, the same half-cell offset every internal upwind face has
  // from its own upstream cell. An inflow face's boundary condition,
  // though, is known exactly *at the face* (zero offset, not a
  // half-cell/full-cell upstream offset) -- using it directly breaks that
  // pattern: differenced against phiOwner and divided by the full cell
  // width (as convection()'s flux-sum/volume does for every face
  // uniformly), the result converges to phi'/2 at the boundary, not phi',
  // an O(1) bias that does not shrink under refinement (confirmed via
  // Taylor expansion and cross-checked numerically --
  // GridRefinementTest.UpwindConvectionConvergesAtFirstOrder's observed
  // order drifted toward 0.5, not the expected ~1, specifically on
  // inflow-boundary-adjacent cells; outflow/tangential-boundary-adjacent
  // and interior cells were already converging correctly).
  //
  // Fix: extrapolate a *ghost* value the same `distance` past the
  // boundary as the owner cell is on this side -- i.e. mirror phiOwner
  // through the exactly-known boundary value, since the boundary sits at
  // the midpoint of [owner, ghost] by construction:
  //   phiBoundary = (phiOwner + phiGhost) / 2  =>  phiGhost = 2*phiBoundary - phiOwner.
  // Using phiGhost (not phiBoundary) as the upwind value restores the
  // same full-spacing offset structure every other face already has, so
  // the boundary flux computed from it is no longer the exact physical
  // flux through this face (that would be faceFlux * phiBoundary) -- it
  // is deliberately the *scheme-consistent* first-order upwind value
  // instead, exactly as every other upwind face in this function returns
  // a cell value standing in for (not equal to) the true face value.
  return (2.0 * phiBoundary) - phiOwner;
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

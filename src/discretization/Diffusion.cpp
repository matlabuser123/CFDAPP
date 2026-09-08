#include "cfd/discretization/Diffusion.hpp"

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::ScalarBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::mesh::Cell;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// Gamma * Af * dphi/dn evaluated from the *owner's* perspective, i.e.
// using the stored Sf direction (owner -> neighbor internally, outward
// for a boundary face). The caller negates this for the neighbor's
// contribution to an internal face.
Real ownerOrientedFlux(const Mesh& mesh, const Face& face, const ScalarField& field,
                       Real diffusivity, const BoundaryConditionSet& boundaries) {
  if (face.isBoundary()) {
    const BoundaryCondition& bc =
        cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
    const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
    if (scalarBc == nullptr) {
      throw InvalidArgumentError("diffusion: boundary condition is not scalar-valued");
    }
    const Cell& owner = mesh.cell(face.owner());
    const Real dPB = MeshGeometry::distance(owner.centroid(), face.centroid());
    const Real phiP = field[face.owner()];
    const Real phiB = scalarBc->boundaryValue(phiP, dPB);

    // The plain two-point secant (phiB - phiP) / dPB estimates dphi/dn
    // at the *midpoint* of P and the face, not at the face itself: it is
    // only first-order accurate there, and -- because that error lives
    // entirely in this one boundary cell's own coefficient -- does not
    // shrink under refinement (an O(1) per-cell bias, not O(h)). Where a
    // third point is available (the interior neighbor across the cell
    // from this boundary face), fit a quadratic through
    // (boundary, owner, opposite-neighbor) instead: exact for quadratics
    // and second-order accurate in general. See TODO.md P0 gate notes.
    const auto oppositeFaceId = MeshGeometry::oppositeInteriorFace(mesh, owner, face);
    if (oppositeFaceId.has_value()) {
      const Face& oppositeFace = mesh.face(*oppositeFaceId);
      const Index farCellId =
          (oppositeFace.owner() == face.owner()) ? *oppositeFace.neighbor() : oppositeFace.owner();
      const Real h1 = dPB;
      const Real h2 = MeshGeometry::ownerNeighborDistance(mesh, oppositeFace);
      const Real phiN = field[farCellId];

      const Real a = (2.0 * h1 + h2) / (h1 * (h1 + h2));
      const Real b = -(h1 + h2) / (h1 * h2);
      const Real c = h1 / (h2 * (h1 + h2));
      const Real dPhiDn = (a * phiB) + (b * phiP) + (c * phiN);
      return diffusivity * face.area() * dPhiDn;
    }

    return diffusivity * face.area() * (phiB - phiP) / dPB;
  }

  const Real dPN = MeshGeometry::ownerNeighborDistance(mesh, face);
  const Real phiP = field[face.owner()];
  const Real phiN = field[*face.neighbor()];
  return diffusivity * face.area() * (phiN - phiP) / dPN;
}

}  // namespace

ScalarField diffusion(const Mesh& mesh, const ScalarField& field, Real diffusivity,
                      const BoundaryConditionSet& boundaries) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("diffusion: field size does not match mesh cell count");
  }

  ScalarField result(mesh.numberOfCells(), 0.0);
  for (const auto& cell : mesh.cells()) {
    Real sum = 0.0;
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Real flux = ownerOrientedFlux(mesh, face, field, diffusivity, boundaries);
      sum += (face.owner() == cell.id()) ? flux : -flux;
    }
    result[cell.id()] = sum / cell.volume();
  }
  return result;
}

}  // namespace cfd::discretization

#pragma once

// P12-DIFF-002 validation migration: ONE shared diagnostic for "the diffusive flux production
// actually assembles through a boundary face", so conservation tests do not each re-derive it.
//
// Why this exists. Several validation instruments used to estimate a boundary diffusive flux as
//     Gamma |S| / d * (phi_P - phi_b)
// (either written inline, or via boundaryFaceDiffusionTerms(..., nullptr, false).coefficient).
// That is the pre-P12-DIFF-002 TWO-POINT wall flux. Production now assembles the DIFF-002
// three-point reconstruction wherever a valid inward stencil exists, so those estimators measured a
// different operator than the solver used and reported a conservation error that the solver did not
// have -- for the species open-channel case, 1.587e-03 where the assembled balance is 3.5e-08
// (results/p12-diff-002/investigation-f/summary.md section 2).
//
// This helper evaluates the SAME operator production uses and assembles the flux from the documented
// convention (results/p12-diff-002/architecture.md section 4): the assembled row holds
// -flux_into_owner, so
//     flux_into_owner = -(coefficient * phi_P - farCellCoefficient * phi_F)
//                       + boundaryValueCoefficient * phi_b + explicitFlux
// which reduces to the historical Gamma |S_orth| / d * (phi_b - phi_P) on a fallback face, where
// farCellCoefficient is 0 and boundaryValueCoefficient equals coefficient.
//
// It is deliberately NOT a production function: production never computes a cross-section flow or a
// per-cell imbalance, so a test that sums these itself still forms its own independent conservation
// statement rather than asking production for the answer it then checks.
#include <algorithm>
#include <cmath>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::testutil {

// The cell gradient production builds for the boundary reconstruction's tangential transfer term.
[[nodiscard]] inline cfd::fields::VectorField diffusionCorrectionGradient(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& phi,
    const cfd::boundary::BoundaryConditionSet& boundaries) {
  return cfd::discretization::gradient(mesh, phi, boundaries,
                                       cfd::discretization::GradientScheme::GreenGauss);
}

// Diffusive flux INTO the owner cell through `face` (a boundary face), evaluated with the operator
// production assembles. `gamma` is the already-resolved diffusion coefficient (k, rho*D, mu, ...).
[[nodiscard]] inline Real boundaryDiffusiveFluxIntoOwner(
    const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face, Real gamma,
    const cfd::fields::ScalarField& phi,
    const cfd::boundary::BoundaryConditionSet& boundaries,
    const cfd::fields::VectorField& gradPhi) {
  const Index owner = face.owner();
  const Real distance =
      cfd::mesh::MeshGeometry::distance(mesh.cell(owner).centroid(), face.centroid());
  const auto& bc = cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
  const auto& scalarBc = dynamic_cast<const cfd::boundary::ScalarBoundaryCondition&>(bc);
  const Real phiB = scalarBc.boundaryValue(phi[owner], distance);
  const bool prescribed = cfd::discretization::prescribesBoundaryValue(bc.type());
  const auto terms = cfd::discretization::boundaryFaceDiffusionTerms(mesh, face, gamma, distance,
                                                                     &gradPhi, prescribed);
  return -((terms.coefficient * phi[owner]) - (terms.farCellCoefficient * phi[terms.farCell])) +
         (terms.boundaryValueCoefficient * phiB) + terms.explicitFlux;
}

// Diffusive flux out of the owner and into the neighbour through an internal face, with the same
// operator. `correctionGradient` is null when the case runs with the non-orthogonal correction
// disabled, exactly as the assemblers pass it.
[[nodiscard]] inline Real internalDiffusiveFluxOutOfOwner(
    const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face, Real gamma,
    const cfd::fields::ScalarField& phi,
    const cfd::fields::VectorField* correctionGradient = nullptr) {
  const Real dPN = cfd::mesh::MeshGeometry::ownerNeighborDistance(mesh, face);
  const Real c =
      cfd::discretization::internalFaceDiffusionTerms(mesh, face, gamma, dPN, correctionGradient)
          .coefficient;
  return c * (phi[face.owner()] - phi[*face.neighbor()]);
}

// Per-cell net diffusive flux (out of each cell) over the whole mesh, from the same operator. The
// assembled steady system drives this to zero for every cell when there is no source, so it is the
// discrete conservation residual a conservation test can bound directly.
[[nodiscard]] inline std::vector<Real> perCellNetDiffusiveOutflow(
    const cfd::mesh::Mesh& mesh, Real gamma, const cfd::fields::ScalarField& phi,
    const cfd::boundary::BoundaryConditionSet& boundaries) {
  const cfd::fields::VectorField gradPhi = diffusionCorrectionGradient(mesh, phi, boundaries);
  std::vector<Real> net(mesh.numberOfCells(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      net[face.owner()] -=
          boundaryDiffusiveFluxIntoOwner(mesh, face, gamma, phi, boundaries, gradPhi);
    } else {
      const Real out = internalDiffusiveFluxOutOfOwner(mesh, face, gamma, phi);
      net[face.owner()] += out;
      net[*face.neighbor()] -= out;
    }
  }
  return net;
}

}  // namespace cfd::testutil

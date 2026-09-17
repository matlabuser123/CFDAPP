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

namespace {

// The two-point form, with P12-DIFF-002's extra fields set to their neutral values: the prescribed
// value carries the same coefficient as the diagonal, and there is no far-cell coupling. Written
// once so every fallback path is identical by construction.
FaceDiffusionTerms twoPointTerms(Real coefficient, Real explicitFlux) {
  FaceDiffusionTerms terms;
  terms.coefficient = coefficient;
  terms.explicitFlux = explicitFlux;
  terms.boundaryValueCoefficient = coefficient;
  terms.farCellCoefficient = 0.0;
  terms.farCell = 0;
  terms.higherOrder = false;
  return terms;
}

}  // namespace

FaceDiffusionTerms internalFaceDiffusionTerms(const Mesh& mesh, const Face& face, Real gammaFace,
                                              Real distance, const VectorField* gradPhi) {
  if (gradPhi != nullptr) {
    const auto decomposition = MeshGeometry::decomposeFaceArea(mesh, face);
    if (decomposition.valid) {
      const Vector2 gradFace = interpolateInternalFace(mesh, face, *gradPhi);
      return twoPointTerms(gammaFace * magnitude(decomposition.orthogonal) / distance,
                           gammaFace * dot(decomposition.nonOrthogonal, gradFace));
    }
  }
  return twoPointTerms(gammaFace * face.area() / distance, 0.0);
}

FaceDiffusionTerms boundaryFaceDiffusionTerms(const Mesh& mesh, const Face& face, Real gammaFace,
                                              Real distance, const VectorField* gradPhi,
                                              bool prescribedValue) {
  if (gradPhi != nullptr && prescribedValue) {
    // P12-DIFF-002 -- second-order one-sided reconstruction of dphi/dn AT THE FACE, from the
    // boundary value, the owner and the far cell across the owner's opposite interior face
    // (results/p12-diff-002/architecture.md). The two-point secant this replaces estimates
    // dphi/dn at the MIDPOINT of P and the face, which is only first-order accurate there --
    // measured as exactly 0.5 h even on a perfectly orthogonal mesh (P12-DIFF-001).
    //
    // With h1, h2 the NORMAL distances from the face to P and to F, a quadratic through the three
    // points gives dphi/ds (s inward) = cP phi~_P - cF phi~_F - cB phi_b, hence
    // dphi/dn = -dphi/ds, and the flux into the owner is Gamma |S| dphi/dn. The assembled row
    // holds -flux, so cP lands on the diagonal, cF couples the owner to the far cell, and cB
    // multiplies the known boundary value. cP - cF = cB identically, so a constant field gives
    // exactly zero flux.
    //
    // phi~ are the cell values transferred onto the wall-normal ray with each cell's own gradient
    // (exactly zero correction on an orthogonal face, where both offsets are exactly {0,0}), which
    // is what makes the normal-direction stencil valid on a non-orthogonal mesh. The transfer is
    // the only lagged part, and it replaces -- never augments -- the former explicit
    // S_nonorth . grad(phi)_P term, so the tangential contribution is not double-counted.
    const auto stencil = MeshGeometry::boundaryInwardStencil(mesh, face);
    if (stencil.valid) {
      const Real h1 = stencil.h1;
      const Real h2 = stencil.h2;
      const Real gammaArea = gammaFace * face.area();
      const Real cP = h2 / (h1 * (h2 - h1));
      const Real cF = h1 / (h2 * (h2 - h1));
      const Real cB = (1.0 / h1) + (1.0 / h2);
      FaceDiffusionTerms terms;
      terms.coefficient = gammaArea * cP;
      terms.farCellCoefficient = gammaArea * cF;
      terms.farCell = stencil.farCell;
      terms.boundaryValueCoefficient = gammaArea * cB;
      terms.explicitFlux =
          gammaArea * ((cP * dot((*gradPhi)[face.owner()], stencil.deltaP)) -
                       (cF * dot((*gradPhi)[stencil.farCell], stencil.deltaF)));
      terms.higherOrder = true;
      return terms;
    }
    // No usable inward stencil (one-cell-thick or degenerate): the pre-existing two-point
    // non-orthogonal treatment, unchanged.
    const auto decomposition = MeshGeometry::decomposeBoundaryFaceArea(mesh, face);
    if (decomposition.valid) {
      return twoPointTerms(
          gammaFace * magnitude(decomposition.orthogonal) / distance,
          gammaFace * dot(decomposition.nonOrthogonal, (*gradPhi)[face.owner()]));
    }
  }
  return twoPointTerms(gammaFace * face.area() / distance, 0.0);
}

}  // namespace cfd::discretization

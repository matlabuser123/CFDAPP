#include "cfd/discretization/Convection.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::discretization {

using cfd::boundary::BoundaryCondition;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::ScalarBoundaryCondition;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Cell;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

ConvectionScheme parseConvectionScheme(std::string_view name) {
  if (name == "upwind") return ConvectionScheme::Upwind;
  if (name == "central") return ConvectionScheme::Central;
  if (name == "linear_upwind") return ConvectionScheme::LinearUpwind;
  if (name == "quick") return ConvectionScheme::QUICK;
  throw InvalidArgumentError(
      "parseConvectionScheme: must be one of upwind, central, linear_upwind, quick (got \"" +
      std::string(name) + "\")");
}

std::string_view convectionSchemeName(ConvectionScheme scheme) noexcept {
  switch (scheme) {
    case ConvectionScheme::Upwind:
      return "upwind";
    case ConvectionScheme::Central:
      return "central";
    case ConvectionScheme::LinearUpwind:
      return "linear_upwind";
    case ConvectionScheme::QUICK:
      return "quick";
  }
  return "upwind";  // unreachable for a valid enum value; never a throw from a noexcept function.
}

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

// --- P12-NUM-001: higher-order internal-face primitives ---------------

Real quickFaceValue(Real phiC, Real hCU, Real phiU, Real hUf, Real phiD, Real hfD) {
  if (!(std::isfinite(hCU) && hCU > 0.0) || !(std::isfinite(hUf) && hUf > 0.0) ||
      !(std::isfinite(hfD) && hfD > 0.0)) {
    throw InvalidArgumentError("quickFaceValue: hCU, hUf, hfD must all be finite and > 0");
  }
  // Value at x=0 (the face) of the unique quadratic through (phiC, phiU,
  // phiD) at their true signed positions x_C = -(hUf+hCU), x_U = -hUf,
  // x_D = +hfD (Lagrange interpolation evaluated at 0) -- reduces to the
  // classical QUICK coefficients (-1/8, 6/8, 3/8) exactly when hUf == hfD
  // == hCU/2 (see this function's own header comment and
  // QuickFaceValueMatchesClassicalCoefficients).
  const Real weightC = (-hUf * hfD) / (hCU * (hUf + hCU + hfD));
  const Real weightU = ((hUf + hCU) * hfD) / (hCU * (hUf + hfD));
  const Real weightD = (hUf * (hUf + hCU)) / ((hUf + hCU + hfD) * (hUf + hfD));
  return (weightC * phiC) + (weightU * phiU) + (weightD * phiD);
}

Real linearUpwindFaceValue(Real phiUpwind, const Vector2& gradPhiUpwind,
                           const Vector2& upwindCentroid, const Vector2& faceCentroid) {
  return phiUpwind + dot(gradPhiUpwind, faceCentroid - upwindCentroid);
}

std::optional<Real> smoothnessRatio(Real phiFarUpstream, Real phiUpwind,
                                    Real phiDownwind) noexcept {
  const Real localGradient = phiDownwind - phiUpwind;
  if (localGradient == 0.0) {
    return std::nullopt;
  }
  return (phiUpwind - phiFarUpstream) / localGradient;
}

Real vanLeerLimiter(std::optional<Real> r) noexcept {
  if (!r.has_value() || *r <= 0.0) {
    return 0.0;
  }
  return (*r + std::abs(*r)) / (1.0 + std::abs(*r));
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

// The far-upstream cell (C in quickFaceValue's terms) for `face`'s
// upwind cell, if one exists as an interior neighbor -- shared by QUICK
// (needs it to construct its own face value) and, via
// smoothnessRatio/vanLeerLimiter, by every non-Upwind scheme (needs it
// only to decide how much of the high-order correction is safe to keep).
struct FarUpstreamCell {
  bool available = false;
  Index cellId = 0;
  Real distanceToUpwind = 0.0;  // hCU
};

FarUpstreamCell findFarUpstreamCell(const Mesh& mesh, const Face& face, Index upwindCellId) {
  const Cell& upwindCell = mesh.cell(upwindCellId);
  const std::optional<Index> farFaceId = MeshGeometry::oppositeInteriorFace(mesh, upwindCell, face);
  if (!farFaceId.has_value()) {
    return {};  // documented fallback: no far-upstream cell available.
  }
  const Face& farFace = mesh.face(*farFaceId);
  const Index farCellId = (farFace.owner() == upwindCellId) ? *farFace.neighbor() : farFace.owner();
  return {true, farCellId, MeshGeometry::ownerNeighborDistance(mesh, farFace)};
}

// The scheme's own (unlimited) high-order face value -- see each case's
// own primitive (quickFaceValue/linearUpwindFaceValue/
// interpolateInternalFace) for the formula. QUICK additionally needs
// `farUpstream` to be available at all (falls back to `phiUpwind` itself
// -- zero correction -- when it is not, a documented deterministic
// degradation, never an out-of-bounds read: P12-NUM-001 requirement 6).
Real highOrderFaceValue(const Mesh& mesh, const Face& face, const ScalarField& field,
                        const std::optional<VectorField>& gradPhi, ConvectionScheme scheme,
                        Index upwindCellId, Index downwindCellId, Real phiUpwind, Real phiDownwind,
                        const FarUpstreamCell& farUpstream) {
  switch (scheme) {
    case ConvectionScheme::Upwind:
      return phiUpwind;  // unreachable from convection() below, kept for completeness.

    case ConvectionScheme::Central:
      return interpolateInternalFace(mesh, face, field);

    case ConvectionScheme::LinearUpwind: {
      const Cell& upwindCell = mesh.cell(upwindCellId);
      return linearUpwindFaceValue(phiUpwind, (*gradPhi)[upwindCellId], upwindCell.centroid(),
                                   face.centroid());
    }

    case ConvectionScheme::QUICK: {
      if (!farUpstream.available) {
        return phiUpwind;  // documented fallback: no far-upstream cell available.
      }
      const Cell& upwindCell = mesh.cell(upwindCellId);
      const Real hUf = MeshGeometry::distance(upwindCell.centroid(), face.centroid());
      const Real hfD =
          MeshGeometry::distance(face.centroid(), mesh.cell(downwindCellId).centroid());
      return quickFaceValue(field[farUpstream.cellId], farUpstream.distanceToUpwind, phiUpwind, hUf,
                            phiDownwind, hfD);
    }
  }
  return phiUpwind;  // unreachable for a valid enum value.
}

}  // namespace

ScalarField convection(const Mesh& mesh, const ScalarField& field, const SurfaceField& faceMassFlux,
                       const BoundaryConditionSet& boundaries, ConvectionScheme scheme) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("convection: field size does not match mesh cell count");
  }
  if (faceMassFlux.size() != mesh.numberOfFaces()) {
    throw InvalidArgumentError("convection: faceMassFlux size does not match mesh face count");
  }

  // Computed once, only when actually needed -- Upwind (the default)
  // pays no extra cost at all, and every existing caller (which never
  // passes a scheme) sees byte-identical behavior to before this
  // parameter existed.
  std::optional<VectorField> gradPhi;
  if (scheme == ConvectionScheme::LinearUpwind) {
    gradPhi = gradient(mesh, field, boundaries);
  }

  ScalarField result(mesh.numberOfCells(), 0.0);
  for (const auto& cell : mesh.cells()) {
    Real sum = 0.0;
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Real ownerFlux = faceMassFlux[faceId];

      Real phiFace;
      if (scheme == ConvectionScheme::Upwind || face.isBoundary()) {
        // Boundary faces always keep the existing ghost-value upwind
        // treatment regardless of scheme (P12-NUM-001 explicit scope
        // limit -- see this file's header comment).
        phiFace = upwindFaceValue(mesh, face, field, ownerFlux, boundaries);
      } else {
        const Index ownerId = face.owner();
        const Index neighborId = *face.neighbor();
        const bool ownerIsUpwind = ownerFlux >= 0.0;
        const Index upwindId = ownerIsUpwind ? ownerId : neighborId;
        const Index downwindId = ownerIsUpwind ? neighborId : ownerId;
        const Real phiUpwind = field[upwindId];
        const Real phiDownwind = field[downwindId];

        const FarUpstreamCell farUpstream = findFarUpstreamCell(mesh, face, upwindId);
        const Real phiHighOrder =
            highOrderFaceValue(mesh, face, field, gradPhi, scheme, upwindId, downwindId, phiUpwind,
                               phiDownwind, farUpstream);

        // P12-NUM-001 boundedness: blend toward phiHighOrder only as far
        // as the local smoothness (Sweby's r) allows -- see
        // smoothnessRatio/vanLeerLimiter's own header comment. r is
        // std::nullopt (limiter -> 0, pure upwind) whenever no far-
        // upstream cell exists. This is NOT merely "no evidence either
        // way, so assume smooth" -- an earlier version of this code
        // defaulted to psi=1 (full blend) in that case, on exactly that
        // reasoning, and it was WRONG: at a boundary-adjacent cell, the
        // OTHER face already uses the boundary's own upwind-consistent
        // "ghost mirror"/"owner value" stand-in (see
        // upwindBoundaryFaceValue's header comment), whose own accuracy
        // relies on EVERY face of that cell using the SAME upwind
        // convention consistently (a telescoping-cancellation property).
        // Blending the other (non-boundary) face toward a genuinely
        // higher-order value breaks that cancellation and introduces an
        // O(1) bias that does NOT shrink under refinement -- confirmed
        // by LinearFieldIsReproducedExactlyAwayFromAnyDegradedFace
        // failing under the psi=1 default (and by hand: the combined
        // stencil converges to 1.5x the true derivative, not 1x, at such
        // a cell). Degrading fully to Upwind there (psi=0) keeps the
        // WHOLE cell on one consistent convention and is exact for
        // affected linear fields, verified by that same test. See
        // results/p12-num-001/summary.md for the full derivation and for
        // why this means the scheme's GLOBAL (whole-domain) observed
        // order is lower than its INTERIOR order for a smooth field.
        const std::optional<Real> r =
            farUpstream.available
                ? smoothnessRatio(field[farUpstream.cellId], phiUpwind, phiDownwind)
                : std::nullopt;
        const Real psi = vanLeerLimiter(r);
        const Real blended = phiUpwind + (psi * (phiHighOrder - phiUpwind));
        phiFace =
            std::clamp(blended, std::min(phiUpwind, phiDownwind), std::max(phiUpwind, phiDownwind));
      }

      const Real cellFlux = (face.owner() == cell.id()) ? ownerFlux : -ownerFlux;
      sum += cellFlux * phiFace;
    }
    result[cell.id()] = sum / cell.volume();
  }
  return result;
}

}  // namespace cfd::discretization

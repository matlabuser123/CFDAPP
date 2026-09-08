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

// Same "most anti-parallel" search MeshGeometry::oppositeInteriorFace does,
// but taking the reference outward direction as a plain Vector2 instead of
// a Face -- needed below to find the *next* interior face continuing away
// from the boundary, one cell further in (from `cell`'s own faces, i.e.
// this is `oppositeInteriorFace` called again from the far side). Passing
// a Face there and letting it derive the direction from that face's own
// stored Sf orientation only works when the reference face is a genuine
// boundary face (no owner/neighbor ambiguity); here the reference
// direction has already been resolved to a proper outward-from-the-
// boundary-cell normal, so it is passed through directly instead of
// re-deriving (and risking mis-signing) it from an interior face.
std::optional<Index> nextInteriorFaceAwayFrom(const Mesh& mesh, const Cell& cell,
                                              const Vector2& awayFromBoundary,
                                              Index excludeFaceId) {
  std::optional<Index> best;
  Real bestDot = 0.0;
  for (const Index faceId : cell.faceIds()) {
    if (faceId == excludeFaceId) continue;
    const Face& candidate = mesh.face(faceId);
    if (candidate.isBoundary()) continue;
    const Vector2 rawNormal = MeshGeometry::unitNormal(candidate);
    const Vector2 outwardFromCell =
        (candidate.owner() == cell.id()) ? rawNormal : (rawNormal * -1.0);
    const Real alignment = dot(awayFromBoundary, outwardFromCell);
    if (!best.has_value() || alignment < bestDot) {
      bestDot = alignment;
      best = faceId;
    }
  }
  return best;
}

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
    if (!oppositeFaceId.has_value()) {
      return diffusivity * face.area() * (phiB - phiP) / dPB;
    }

    const Face& oppositeFace = mesh.face(*oppositeFaceId);
    const Index farCellId =
        (oppositeFace.owner() == face.owner()) ? *oppositeFace.neighbor() : oppositeFace.owner();
    const Real h1 = dPB;
    const Real h2 = MeshGeometry::ownerNeighborDistance(mesh, oppositeFace);
    const Real phiN = field[farCellId];

    // The 3-point quadratic fit above makes dphi/dn *at the boundary
    // face* second-order accurate, and that alone is already enough for
    // every analytical-exactness test (quadratic fields, section P0 gate
    // notes). It is NOT enough to make the resulting *Laplacian* (this
    // boundary cell's divergence-of-flux, i.e. a second derivative)
    // second-order for a general smooth field: h1 != h2 here (h1 = h2/2
    // on a uniform grid, since the boundary face sits half a cell short
    // of a full interior spacing), and a second-derivative estimate that
    // combines a 3-point one-sided flux at this face with the standard
    // 2-point central flux at the opposite face is mathematically capped
    // at first order whenever the two spacings differ (confirmed by
    // Taylor expansion; see TODO.md P0 gate notes and
    // GridRefinementTest.LaplacianOfSmoothFieldConvergesAtSecondOrder).
    //
    // Fix: reach one cell further (the far neighbor's own far neighbor,
    // "N2") and fit a *cubic* through all 4 points (boundary, owner,
    // N1, N2) via Newton divided differences, which gives a genuinely
    // second-order estimate of d2(phi)/dn2 *at the owner cell P* (not at
    // the boundary) directly -- then back out what this boundary face's
    // own dphi/dn would have to be for the existing (unchanged) opposite-
    // face central-difference flux, differenced with this face's flux
    // and divided by the cell width, to reproduce that d2(phi)/dn2
    // exactly. This changes only the boundary face's own flux value (used
    // by nobody but this cell), so the interior face's flux -- shared
    // with N1's own sum, negated -- is untouched and pairwise
    // conservation there is unaffected (DiffusionTest.
    // ZeroFluxBoundaryConservesGlobally).
    const auto boundaryOutward = MeshGeometry::unitNormal(face);
    const auto secondFaceId =
        nextInteriorFaceAwayFrom(mesh, mesh.cell(farCellId), boundaryOutward, *oppositeFaceId);
    if (secondFaceId.has_value()) {
      const Face& secondFace = mesh.face(*secondFaceId);
      const Index n2CellId =
          (secondFace.owner() == farCellId) ? *secondFace.neighbor() : secondFace.owner();
      const Real h3 = MeshGeometry::ownerNeighborDistance(mesh, secondFace);
      const Real phiN2 = field[n2CellId];

      // Newton divided differences on nodes at local positions (relative
      // to P) d0=-h1 (B), d1=0 (P), d2=h2 (N1), d3=h2+h3 (N2).
      const Real f01 = (phiP - phiB) / h1;
      const Real f12 = (phiN - phiP) / h2;
      const Real f23 = (phiN2 - phiN) / h3;
      const Real f012 = (f12 - f01) / (h1 + h2);
      const Real f123 = (f23 - f12) / (h2 + h3);
      const Real f0123 = (f123 - f012) / (h1 + h2 + h3);
      // d/dn^2(phi) at P, from the cubic interpolant through all 4
      // points -- exact (zero error) whenever phi is itself a cubic or
      // lower degree, so this also preserves (and generalizes) the
      // existing quadratic-exactness tests.
      const Real d2PhiDn2AtP = (2.0 * f012) - (2.0 * (h2 - h1) * f0123);

      // f01..d2PhiDn2AtP above are all expressed in the "into the domain"
      // local coordinate xi (0 at the boundary, increasing through P, N1,
      // N2) -- the natural frame for a Newton divided-difference fit
      // through those 4 points in that order. d2PhiDn2AtP, a *second*
      // derivative, is unaffected by that choice (d2/dxi2 == d2/dn2 under
      // the linear reflection n = -xi). But dPhiDnEast/dPhiDnXi *are*
      // first derivatives, so they are in xi's sense, not outward-normal
      // n's -- and this function's contract (matched by the 2-point
      // interior-face formula above and the 3-point fallback below, and
      // confirmed by hand against the latter for phi=x) is to return
      // dphi/dn, i.e. the negative of dphi/dxi. Confirmed by hand: for
      // phi=x this produced +1 (dphi/dxi) pre-negation where the 3-point
      // formula below (already correct) gives -1 (dphi/dn) for the same
      // input -- omitting this negation is exactly what produced the
      // large, refinement-growing errors seen before this comment was
      // added.
      const Real dPhiDnEast = f12;  // = (phiN - phiP) / h2, unchanged central difference
      const Real cellWidth = MeshGeometry::distance(face.centroid(), oppositeFace.centroid());
      const Real dPhiDnXi = dPhiDnEast - (cellWidth * d2PhiDn2AtP);
      const Real dPhiDnWest = -dPhiDnXi;
      return diffusivity * face.area() * dPhiDnWest;
    }

    // Mesh too narrow (< 3 cells) in this direction for a 4th point --
    // fall back to the 3-point quadratic-fit flux, still exact for
    // quadratics and better than the plain two-point secant, just not
    // second-order for the Laplacian of a general smooth field.
    const Real a = (2.0 * h1 + h2) / (h1 * (h1 + h2));
    const Real b = -(h1 + h2) / (h1 * h2);
    const Real c = h1 / (h2 * (h1 + h2));
    const Real dPhiDn = (a * phiB) + (b * phiP) + (c * phiN);
    return diffusivity * face.area() * dPhiDn;
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

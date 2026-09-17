#include "cfd/discretization/Gradient.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
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

GradientScheme parseGradientScheme(std::string_view name) {
  if (name == "green_gauss") return GradientScheme::GreenGauss;
  if (name == "least_squares") return GradientScheme::LeastSquares;
  throw InvalidArgumentError(
      "parseGradientScheme: must be one of green_gauss, least_squares (got \"" + std::string(name) +
      "\")");
}

std::string_view gradientSchemeName(GradientScheme scheme) noexcept {
  switch (scheme) {
    case GradientScheme::GreenGauss:
      return "green_gauss";
    case GradientScheme::LeastSquares:
      return "least_squares";
  }
  return "green_gauss";  // unreachable for a valid enum value; never a throw from a noexcept
                         // function.
}

namespace {

// P12-GRAD-002 -- boundary-consistent face value. Full derivation:
// results/p12-grad-002/formulation.md.
//
// Plain Green-Gauss gradient mixes an *exact* boundary value (zero error)
// with an *interpolated* opposite-face value (O(h^2) error) in the same
// central-difference-shaped sum. That mismatch -- not any asymmetry in
// face positions -- leaves an O(h) bias in the boundary cell's own
// gradient (confirmed by Taylor expansion: the boundary-adjacent
// component drops from the interior's clean 2nd order to 1st order).
// This is a known limitation of plain Green-Gauss gradients at
// boundaries. Green-Gauss is exact for the volume-averaged gradient
// whenever every face value is exact, so the fix belongs in the FACE
// VALUE, not in the sum: remove that leading O(h^2) interpolation bias
// from the opposite face's value and the pairwise cancellation that gives
// the interior its second order is restored at the boundary too.
//
// Linear interpolation along the owner-neighbor line has the exact error
//   phi(x_f') - phi_interp(x_f') = -1/2 w (1-w) L^2 d2phi/dxi^2 + O(h^3)
// with L = |x_F - x_P|, xi = (x_F - x_P)/L and w the crossing-point
// interpolation weight (w(1-w) is symmetric, so which cell owns the face
// does not matter). The second derivative along xi comes from the three
// points a boundary cell actually has along that line: the far cell F,
// the owner P, and the point B' where the line through P along -xi meets
// the boundary face's PLANE -- not the boundary face centroid, which only
// lies on that line when the mesh happens to be orthogonal there.
//
// Why this replaces the former treatment rather than extending it: the
// old code REPLACED the {boundary, opposite} contribution pair with a
// one-dimensional fit along the boundary normal, which is only the
// correct "normal part" of the Green-Gauss sum when both faces are
// parallel to that normal AND carry equal areas. That Cartesian
// assumption sat in the structure of the treatment, so it had to be
// selected by geometric tests -- an exact `cross(d, S_f) == 0`, then
// P12-GRAD-001's tolerance on the normalized misalignment, plus a
// relative equal-area test. Both branches of such a test differ at
// LEADING order (second-order versus first-order boundary gradient), so
// crossing it moved the solution by O(h) however small the geometric
// perturbation that caused the crossing: a Cartesian mesh translated by a
// non-representable offset, or moved by P12-MESH-007's mesh motion,
// silently changed discretization (P12-MESH-007 G6.3;
// results/p12-grad-001/summary.md). No cutoff value fixes that -- round-off
// misalignment reaches 7.9e-4 at X/h ~ 2e4, above any cutoff that still
// rejects genuine non-orthogonality -- so the cutoff itself is gone. Every
// quantity below is a smooth function of the coordinates with bounded
// denominators, and the correction is exactly zero when the tangential
// offset is exactly zero, so the formulation is continuous in the geometry
// and reduces to the SAME linear functional as the old paired fit on an
// aligned Cartesian mesh (formulation.md section 4.1).
struct BoundaryConsistentFaceValue {
  bool applies = false;
  Index faceId = 0;
  Real value = 0.0;
  Real antiParallelAlignment = 0.0;  // diagnostic: margin of the opposite-face choice
};

BoundaryConsistentFaceValue boundaryConsistentFaceValue(const Mesh& mesh, const Cell& cell,
                                                        const Face& boundaryFace,
                                                        const ScalarField& field,
                                                        const SurfaceField& faceValues,
                                                        const Vector2& ownerGradient) {
  const auto oppositeFaceId = MeshGeometry::oppositeInteriorFace(mesh, cell, boundaryFace);
  if (!oppositeFaceId.has_value()) {
    return {};  // topological: no interior face across the cell (e.g. a 1-cell-thick domain).
  }
  const Face& oppositeFace = mesh.face(*oppositeFaceId);
  const Index farCellId =
      (oppositeFace.owner() == cell.id()) ? *oppositeFace.neighbor() : oppositeFace.owner();

  const Vector2 d = mesh.cell(farCellId).centroid() - cell.centroid();
  const Real farDistance = magnitude(d);
  if (!(farDistance > 0.0)) {
    return {};  // coincident centroids -- degenerate, rejected by MeshQuality.
  }
  const Vector2 inward = d * (1.0 / farDistance);

  // The crossing weight is the same quantity P12-NUM-003's skewness
  // correction uses; std::nullopt is its documented `d . Sf` degeneracy.
  const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, oppositeFace);
  if (!crossing.has_value()) {
    return {};
  }
  const auto intersection = MeshGeometry::boundaryLineIntersection(mesh, boundaryFace, inward);
  if (!intersection.valid) {
    return {};
  }

  // Value at B', the boundary point ON the fit line: the boundary face's own
  // value (so Dirichlet, Neumann and P12-MESH-001's oblique-Neumann treatment
  // are all inherited unchanged) transferred along the face by the owner's
  // gradient. The offset is exactly zero on an aligned face, which makes the
  // whole correction reduce to the aligned-Cartesian formula bit for bit.
  const Real phiBoundary = faceValues[boundaryFace.id()] +
                           dot(ownerGradient, intersection.point - boundaryFace.centroid());

  const Real phiP = field[cell.id()];
  const Real phiFar = field[farCellId];
  const Real backDistance = intersection.distance;

  // Second derivative along `inward` from the three points at (-backDistance,
  // 0, +farDistance): 2 [ (phi_B' - phi_P)/t + (phi_F - phi_P)/L ] / (t + L).
  const Real secondDerivative =
      2.0 * (((phiBoundary - phiP) / backDistance) + ((phiFar - phiP) / farDistance)) /
      (backDistance + farDistance);

  const Real w = crossing->t;
  const Real correction = -0.5 * w * (1.0 - w) * farDistance * farDistance * secondDerivative;

  const Vector2 referenceNormal = MeshGeometry::unitNormal(boundaryFace);
  const Vector2 oppositeNormal = (oppositeFace.owner() == cell.id())
                                     ? MeshGeometry::unitNormal(oppositeFace)
                                     : (MeshGeometry::unitNormal(oppositeFace) * -1.0);
  return {true, *oppositeFaceId, faceValues[*oppositeFaceId] + correction,
          dot(referenceNormal, oppositeNormal)};
}

// P12-MESH-001: a boundary face with a Neumann-type (not value-
// prescribing, see prescribesBoundaryValue) condition whose owner-to-face
// vector d = x_f - x_P is not parallel to its area vector -- the grid line
// through the owner meets the boundary obliquely, as on a production
// structured_quad mesh. The condition's boundaryValue(phiP, distance)
// extrapolates along the boundary NORMAL (phi_P + g * distance); taken
// as the value at the face centroid with the straight-line |d| (the
// pre-existing treatment), it silently sets the tangential variation of
// phi between P and the face to zero -- an O(1) relative error in the
// boundary cell's gradient that does not shrink under refinement
// (measured on a conduction slab with adiabatic walls met at ~30-49
// degrees: temperature error stalled at ~4e-2 of 20 K from 20x4 to 40x8,
// versus 1e-3 on a mesh orthogonal at the walls --
// results/p12-mesh-001/summary.md). Split d instead into its normal part
// d_n = d . n and the tangential rest d_t = d - d_n n, so the face value
// is boundaryValue(phiP, d_n) + grad(phi)_P . d_t -- exact for any
// linear field satisfying the condition. `applies` is false (the
// pre-existing treatment, bit-identical) for every exactly-parallel face,
// i.e. every boundary face of MeshGeometry::createCartesian2D.
struct ObliqueNeumannFace {
  bool applies{false};
  Real normalDistance{0.0};
  Vector2 unitNormal{0.0, 0.0};
  Vector2 tangentialOffset{0.0, 0.0};
};

ObliqueNeumannFace obliqueNeumannFace(const Mesh& mesh, const Face& face,
                                      const BoundaryConditionSet& boundaries) {
  if (prescribesBoundaryValue(
          cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries).type())) {
    return {};
  }
  const Vector2 d = face.centroid() - mesh.cell(face.owner()).centroid();
  const Vector2 n = MeshGeometry::unitNormal(face);
  const Real dn = dot(d, n);
  if (!(dn > 0.0)) {
    return {};  // inverted face -- rejected by MeshQuality before any solve.
  }
  // P12-GRAD-002: this used to return early on `cross(d, sf) == Vector3{}`,
  // selecting between the general formula below and interpolate()'s
  // straight-line one. The test is now on the tangential offset the general
  // formula actually needs, and it is an ITERATION TRIGGER, not a choice
  // between two formulations: when the offset is exactly zero the two agree
  // bit for bit -- boundaryValue(phi_P, d . n) with d . n == |d| (sqrt(x*x)
  // == |x| for every normal double) plus an exactly-zero tangential term --
  // so no face is discretized differently either side of it, and the sweeps
  // below are simply skipped where they would change nothing. See
  // results/p12-grad-002/formulation.md section 4.2 and criterion C12.
  const Vector2 tangentialOffset = d - (n * dn);
  if (tangentialOffset == Vector3{}) {
    return {};
  }
  return {true, dn, n, tangentialOffset};
}

const ScalarBoundaryCondition& scalarConditionForFace(const Mesh& mesh, const Face& face,
                                                      const BoundaryConditionSet& boundaries) {
  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, face.id(), boundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("gradient: boundary condition is not scalar-valued");
  }
  return *scalarBc;
}

// P12-MESH-005: the 3D weighted least-squares system -- the same
// normal equations, weight 1/|d|^2 and zero-distance skip as the 2D path,
// with the z row/column: S g = b, S = sum w d d^T (symmetric 3 x 3),
// b = sum w d dphi, solved by the adjugate (cofactor) formula
// g = adj(S) b / det(S). The singularity guard is the 2D one with the
// matching power: det(S) and scale^3 (scale = trace S) have the same
// units, so det < 1e-10 scale^3 is a dimensionless test; it rejects
// coplanar or colinear displacement sets (det = 0 up to round-off). The
// factor 1e-10 is carried over unchanged from the 2D path (not tuned for
// 3D); a Cartesian hexahedron gives det / trace^3 = 8/216 ~ 0.037.
LeastSquaresGradientResult solveLeastSquaresGradient3D(const std::vector<Vector3>& displacements,
                                                       const std::vector<Real>& valueDifferences) {
  Real Sxx = 0.0;
  Real Sxy = 0.0;
  Real Sxz = 0.0;
  Real Syy = 0.0;
  Real Syz = 0.0;
  Real Szz = 0.0;
  Real bx = 0.0;
  Real by = 0.0;
  Real bz = 0.0;
  for (std::size_t i = 0; i < displacements.size(); ++i) {
    const Real dx = displacements[i].x;
    const Real dy = displacements[i].y;
    const Real dz = displacements[i].z;
    const Real distanceSquared = (dx * dx) + (dy * dy) + (dz * dz);
    if (!(distanceSquared > 0.0)) {
      continue;
    }
    const Real weight = 1.0 / distanceSquared;
    const Real dphi = valueDifferences[i];
    Sxx += weight * dx * dx;
    Sxy += weight * dx * dy;
    Sxz += weight * dx * dz;
    Syy += weight * dy * dy;
    Syz += weight * dy * dz;
    Szz += weight * dz * dz;
    bx += weight * dx * dphi;
    by += weight * dy * dphi;
    bz += weight * dz * dphi;
  }

  // Cofactors of the symmetric matrix [[Sxx Sxy Sxz] [Sxy Syy Syz] [Sxz Syz Szz]].
  const Real c11 = (Syy * Szz) - (Syz * Syz);
  const Real c12 = (Sxz * Syz) - (Sxy * Szz);
  const Real c13 = (Sxy * Syz) - (Sxz * Syy);
  const Real c22 = (Sxx * Szz) - (Sxz * Sxz);
  const Real c23 = (Sxy * Sxz) - (Sxx * Syz);
  const Real c33 = (Sxx * Syy) - (Sxy * Sxy);
  const Real determinant = (Sxx * c11) + (Sxy * c12) + (Sxz * c13);
  const Real scale = Sxx + Syy + Szz;
  if (!(scale > 0.0) || determinant < (1e-10 * scale * scale * scale)) {
    return {Vector3{0.0, 0.0, 0.0}, false};
  }
  const Real gx = ((c11 * bx) + (c12 * by) + (c13 * bz)) / determinant;
  const Real gy = ((c12 * bx) + (c22 * by) + (c23 * bz)) / determinant;
  const Real gz = ((c13 * bx) + (c23 * by) + (c33 * bz)) / determinant;
  if (!std::isfinite(gx) || !std::isfinite(gy) || !std::isfinite(gz)) {
    return {Vector3{0.0, 0.0, 0.0}, false};  // defensive backstop, as in the 2D path.
  }
  return {Vector3{gx, gy, gz}, true};
}

}  // namespace

// --- P12-NUM-002: weighted least-squares gradient reconstruction. -------

LeastSquaresGradientResult solveLeastSquaresGradient(const std::vector<Vector2>& displacements,
                                                     const std::vector<Real>& valueDifferences) {
  if (displacements.size() != valueDifferences.size()) {
    throw InvalidArgumentError(
        "solveLeastSquaresGradient: displacements and valueDifferences must be the same size");
  }

  // P12-MESH-005: displacements with a z component (a 3D cell) take the
  // 3 x 3 system below; planar ones (every 2D cell) the unchanged 2 x 2
  // system, so 2D gradients are bit-identical.
  const bool threeDimensional = std::any_of(displacements.begin(), displacements.end(),
                                            [](const Vector3& d) { return d.z != 0.0; });
  if (threeDimensional) {
    return solveLeastSquaresGradient3D(displacements, valueDifferences);
  }

  // Normal-equations accumulation -- see this function's own header
  // comment for the formulas. Weight = 1/|displacement|^2.
  Real Sxx = 0.0;
  Real Sxy = 0.0;
  Real Syy = 0.0;
  Real bx = 0.0;
  Real by = 0.0;
  for (std::size_t i = 0; i < displacements.size(); ++i) {
    const Real dx = displacements[i].x;
    const Real dy = displacements[i].y;
    const Real distanceSquared = (dx * dx) + (dy * dy);
    if (!(distanceSquared > 0.0)) {
      continue;  // a zero-distance "neighbor" carries no directional information -- skip it.
    }
    const Real weight = 1.0 / distanceSquared;
    const Real dphi = valueDifferences[i];
    Sxx += weight * dx * dx;
    Sxy += weight * dx * dy;
    Syy += weight * dy * dy;
    bx += weight * dx * dphi;
    by += weight * dy * dphi;
  }

  const Real determinant = (Sxx * Syy) - (Sxy * Sxy);
  const Real scale = Sxx + Syy;
  // Scale-invariant singularity/ill-conditioning threshold: `determinant`
  // has units of (weight*distance^2)^2, matching `scale*scale` -- their
  // ratio is dimensionless regardless of mesh spacing or field
  // magnitude. Small (near-zero) whenever every contributing
  // displacement is (near-)colinear -- see this function's own header
  // comment.
  if (!(scale > 0.0) || determinant < (1e-10 * scale * scale)) {
    return {Vector2{0.0, 0.0}, false};
  }

  const Real gx = ((bx * Syy) - (by * Sxy)) / determinant;
  const Real gy = ((Sxx * by) - (Sxy * bx)) / determinant;
  if (!std::isfinite(gx) || !std::isfinite(gy)) {
    // Should be unreachable given the conditioning guard above (finite
    // inputs + a non-tiny determinant cannot produce a non-finite
    // quotient) -- kept as a defensive, tested backstop: P12-NUM-002
    // requirement "never produce NaN/Inf silently" holds even if that
    // reasoning is ever wrong for some future input.
    return {Vector2{0.0, 0.0}, false};
  }
  return {Vector2{gx, gy}, true};
}

VectorField leastSquaresGradient(const Mesh& mesh, const ScalarField& field,
                                 const BoundaryConditionSet& boundaries) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("leastSquaresGradient: field size does not match mesh cell count");
  }

  // Computed once for the whole mesh, used only as the per-cell fallback
  // for a singular/ill-conditioned local least-squares system (see this
  // function's own header comment on the fallback policy) -- never
  // discarded work if no cell ever needs it, since GreenGauss's own cost
  // is the same order as least-squares' own assembly.
  const VectorField greenGaussFallback = gradient(mesh, field, boundaries);

  VectorField result(mesh.numberOfCells(), Vector2{0.0, 0.0});
  for (const auto& cell : mesh.cells()) {
    std::vector<Vector2> displacements;
    std::vector<Real> valueDifferences;
    displacements.reserve(cell.faceIds().size());
    valueDifferences.reserve(cell.faceIds().size());

    for (const Index faceId : cell.faceIds()) {
      const Face& face = mesh.face(faceId);
      if (!face.isBoundary()) {
        const Index neighborId = (face.owner() == cell.id()) ? *face.neighbor() : face.owner();
        displacements.push_back(mesh.cell(neighborId).centroid() - cell.centroid());
        valueDifferences.push_back(field[neighborId] - field[cell.id()]);
        continue;
      }

      // Boundary "virtual neighbor": the assigned condition's own value
      // at the face's own true position -- distinct from the GreenGauss
      // path's ghost-mirror/paired-quadratic trick above, and not
      // needed here: least-squares only wants a (position, value) pair,
      // and the boundary face's own centroid IS an exactly-known
      // position with an exactly-known value (via boundaryValue), no
      // extrapolation required.
      const BoundaryCondition& bc =
          cfd::boundary::boundaryConditionForFace(mesh, faceId, boundaries);
      const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
      if (scalarBc == nullptr) {
        throw InvalidArgumentError("leastSquaresGradient: boundary condition is not scalar-valued");
      }
      // P12-MESH-001: an oblique Neumann-type face (see ObliqueNeumannFace)
      // contributes the condition's value at the foot of the normal from P,
      // x_P + d_n n (on the face's line) -- the row n . grad(phi) = g,
      // exact for linear fields -- instead of at the face centroid.
      const ObliqueNeumannFace oblique = obliqueNeumannFace(mesh, face, boundaries);
      if (oblique.applies) {
        displacements.push_back(oblique.unitNormal * oblique.normalDistance);
        valueDifferences.push_back(
            scalarBc->boundaryValue(field[cell.id()], oblique.normalDistance) - field[cell.id()]);
        continue;
      }
      const Real distance = MeshGeometry::distance(cell.centroid(), face.centroid());
      const Real phiBoundary = scalarBc->boundaryValue(field[cell.id()], distance);
      displacements.push_back(face.centroid() - cell.centroid());
      valueDifferences.push_back(phiBoundary - field[cell.id()]);
    }

    const LeastSquaresGradientResult localResult =
        solveLeastSquaresGradient(displacements, valueDifferences);
    result[cell.id()] =
        localResult.wellConditioned ? localResult.gradient : greenGaussFallback[cell.id()];
  }
  return result;
}

namespace {

// One Green-Gauss summation over the given face values -- the
// pre-P12-NUM-003 per-cell loop, factored out so the skewness-corrected
// gradient below can re-run it.
//
// P12-GRAD-002: a boundary cell's interior face values are made
// boundary-consistent first (boundaryConsistentFaceValue above), CELL-LOCALLY
// -- the corrected value belongs to this cell's reconstruction, exactly as the
// paired contribution it supersedes did. `previousGradient` is the last sweep's
// gradient, used only for the tangential transfer of the boundary value along
// its own face; it is null on the first sweep, where that offset's contribution
// is dropped (it is exactly zero on an aligned face, so an aligned mesh needs no
// sweep at all). A cell with no boundary face takes the unchanged code path
// below, so interior cells are bit-identical to the pre-GRAD-002 library.
// P12-GRAD-002: does any boundary face's fit line meet the boundary away from
// that face's own centroid? Only then does boundaryConsistentFaceValue's
// tangential transfer contribute anything, and only then can a second sweep
// change the result. An iteration trigger, not a formulation choice: when every
// offset is exactly zero the transfer term is exactly 0.0 and the extra sweeps
// would reproduce the first one bit for bit.
bool boundaryTransferNeeded(const Mesh& mesh) {
  for (const auto& cell : mesh.cells()) {
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      if (!face.isBoundary()) {
        continue;
      }
      const auto oppositeFaceId = MeshGeometry::oppositeInteriorFace(mesh, cell, face);
      if (!oppositeFaceId.has_value()) {
        continue;
      }
      const Face& oppositeFace = mesh.face(*oppositeFaceId);
      const Index farCellId =
          (oppositeFace.owner() == cell.id()) ? *oppositeFace.neighbor() : oppositeFace.owner();
      const Vector2 d = mesh.cell(farCellId).centroid() - cell.centroid();
      const Real farDistance = magnitude(d);
      if (!(farDistance > 0.0)) {
        continue;
      }
      const auto intersection =
          MeshGeometry::boundaryLineIntersection(mesh, face, d * (1.0 / farDistance));
      if (intersection.valid && (intersection.point - face.centroid()) != Vector3{}) {
        return true;
      }
    }
  }
  return false;
}

VectorField greenGaussSweep(const Mesh& mesh, const ScalarField& field,
                            const SurfaceField& faceValues, const VectorField* previousGradient) {
  VectorField result(mesh.numberOfCells(), Vector2{0.0, 0.0});
  // faceId -> (corrected value, anti-parallel alignment, claiming boundary face),
  // reused across cells to keep this off the per-cell allocation path.
  std::vector<std::tuple<Index, Real, Real, Index>> corrected;
  for (const auto& cell : mesh.cells()) {
    Vector2 sum{0.0, 0.0};

    corrected.clear();
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      if (!face.isBoundary()) {
        continue;
      }
      const Vector2 ownerGradient =
          (previousGradient == nullptr) ? Vector2{0.0, 0.0} : (*previousGradient)[cell.id()];
      const BoundaryConsistentFaceValue consistent =
          boundaryConsistentFaceValue(mesh, cell, face, field, faceValues, ownerGradient);
      if (!consistent.applies) {
        continue;
      }
      // Two boundary faces of the same cell can only claim the same opposite
      // face on a cell no structured generator produces; resolve it
      // deterministically (most anti-parallel wins, ties by lowest boundary
      // face id) rather than by face iteration order.
      const auto existing =
          std::find_if(corrected.begin(), corrected.end(),
                       [&](const auto& entry) { return std::get<0>(entry) == consistent.faceId; });
      const auto claim = std::make_tuple(consistent.faceId, consistent.value,
                                         consistent.antiParallelAlignment, faceId);
      if (existing == corrected.end()) {
        corrected.push_back(claim);
      } else if (std::make_pair(consistent.antiParallelAlignment, faceId) <
                 std::make_pair(std::get<2>(*existing), std::get<3>(*existing))) {
        *existing = claim;
      }
    }

    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const Vector2 sfCell =
          (face.owner() == cell.id()) ? face.areaVector() : (face.areaVector() * -1.0);
      const auto entry = std::find_if(corrected.begin(), corrected.end(), [&](const auto& item) {
        return std::get<0>(item) == faceId;
      });
      sum += sfCell * ((entry == corrected.end()) ? faceValues[faceId] : std::get<1>(*entry));
    }

    result[cell.id()] = sum * (1.0 / cell.volume());
  }
  return result;
}

}  // namespace

VectorField greenGaussGradient(const Mesh& mesh, const ScalarField& field,
                               const BoundaryConditionSet& boundaries, Index skewCorrectionSweeps) {
  if (field.size() != mesh.numberOfCells()) {
    throw InvalidArgumentError("gradient: field size does not match mesh cell count");
  }

  SurfaceField faceValues = interpolate(mesh, field, boundaries);
  VectorField result = greenGaussSweep(mesh, field, faceValues, nullptr);

  // P12-NUM-003 skewness correction: only faces whose skew vector is not
  // exactly zero are touched (MeshGeometry::ownerNeighborCrossing reports
  // an exact zero on every face of an orthogonal mesh), so on such a mesh
  // this returns the single sweep above -- bit-identical to the
  // pre-P12-NUM-003 Green-Gauss gradient.
  std::vector<Index> skewedFaces;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      continue;
    }
    const auto crossing = MeshGeometry::ownerNeighborCrossing(mesh, face);
    if (crossing.has_value() && crossing->skewVector != Vector3{}) {
      skewedFaces.push_back(face.id());
    }
  }
  // P12-MESH-001: oblique Neumann-type boundary faces (ObliqueNeumannFace)
  // are re-evaluated in the same sweeps with the latest owner gradient.
  // None exist on a Cartesian mesh, so it stays bit-identical.
  std::vector<std::pair<Index, ObliqueNeumannFace>> obliqueFaces;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      continue;
    }
    const ObliqueNeumannFace oblique = obliqueNeumannFace(mesh, face, boundaries);
    if (oblique.applies) {
      obliqueFaces.emplace_back(face.id(), oblique);
    }
  }
  // P12-GRAD-002: the boundary-consistent face value needs the owner gradient
  // only for its tangential transfer, which is exactly zero unless some fit
  // line meets the boundary away from the face centroid -- so the sweeps are
  // triggered by that and by nothing else. Either side of this test the
  // boundary value is bit-identical (the transfer term is exactly 0.0), so it
  // never selects between formulations; an aligned Cartesian mesh keeps its
  // single sweep and its cost.
  const bool boundaryTransfer = boundaryTransferNeeded(mesh);
  for (Index sweep = 0; sweep < skewCorrectionSweeps &&
                        (!skewedFaces.empty() || !obliqueFaces.empty() || boundaryTransfer);
       ++sweep) {
    for (const Index faceId : skewedFaces) {
      faceValues[faceId] =
          interpolateInternalFaceSkewCorrected(mesh, mesh.face(faceId), field, result);
    }
    for (const auto& [faceId, oblique] : obliqueFaces) {
      const Face& face = mesh.face(faceId);
      const Real phiP = field[face.owner()];
      faceValues[faceId] = scalarConditionForFace(mesh, face, boundaries)
                               .boundaryValue(phiP, oblique.normalDistance) +
                           dot(result[face.owner()], oblique.tangentialOffset);
    }
    result = greenGaussSweep(mesh, field, faceValues, &result);
  }
  return result;
}

VectorField gradient(const Mesh& mesh, const ScalarField& field,
                     const BoundaryConditionSet& boundaries, GradientScheme scheme) {
  if (scheme == GradientScheme::LeastSquares) {
    return leastSquaresGradient(mesh, field, boundaries);
  }
  // GreenGauss: skewness-corrected since P12-NUM-003 (identical to the
  // original formula on any mesh without skewed faces -- see
  // greenGaussGradient's own header comment).
  return greenGaussGradient(mesh, field, boundaries, kGreenGaussSkewCorrectionSweeps);
}

}  // namespace cfd::discretization

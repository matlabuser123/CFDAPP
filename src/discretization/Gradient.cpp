#include "cfd/discretization/Gradient.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
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

// Plain Green-Gauss gradient mixes an *exact* boundary value (zero error)
// with an *interpolated* opposite-face value (O(h^2) error) in the same
// central-difference-shaped sum. That mismatch -- not any asymmetry in
// face positions -- leaves an O(h) bias in the boundary cell's own
// gradient (confirmed by Taylor expansion: the boundary-adjacent
// component drops from the interior's clean 2nd order to 1st order).
// This is a known limitation of plain Green-Gauss gradients at
// boundaries. Where an interior neighbor sits directly opposite the
// boundary face (true for every non-degenerate cell on an orthogonal
// Cartesian mesh), replace the {boundary face, opposite face}
// contribution pair with the exact directional derivative from a
// quadratic fit through (boundary, owner, opposite neighbor) -- second
// order in general, exact for quadratics, matching the interior scheme.
struct PairedBoundaryContribution {
  bool applies = false;
  Index oppositeFaceId = 0;
  Vector2 contribution{0.0, 0.0};
};

PairedBoundaryContribution tryPairedBoundaryContribution(const Mesh& mesh, const Cell& cell,
                                                         const Face& boundaryFace,
                                                         const ScalarField& field,
                                                         const BoundaryConditionSet& boundaries) {
  const auto oppositeFaceId = MeshGeometry::oppositeInteriorFace(mesh, cell, boundaryFace);
  if (!oppositeFaceId.has_value()) {
    return {};
  }
  const Face& oppositeFace = mesh.face(*oppositeFaceId);

  // The shortcut below assumes the boundary face and its opposite share
  // the same area (exactly true for an orthogonal Cartesian quad cell,
  // where both faces bound the same row/column width). If a future mesh
  // breaks that, fall back to the ordinary per-face treatment rather
  // than silently misapplying the formula.
  if (std::abs(boundaryFace.area() - oppositeFace.area()) > 1e-12 * boundaryFace.area()) {
    return {};
  }

  const BoundaryCondition& bc =
      cfd::boundary::boundaryConditionForFace(mesh, boundaryFace.id(), boundaries);
  const auto* scalarBc = dynamic_cast<const ScalarBoundaryCondition*>(&bc);
  if (scalarBc == nullptr) {
    throw InvalidArgumentError("gradient: boundary condition is not scalar-valued");
  }

  const Real h1 = MeshGeometry::distance(cell.centroid(), boundaryFace.centroid());
  const Real h2 = MeshGeometry::ownerNeighborDistance(mesh, oppositeFace);
  const Index farCellId =
      (oppositeFace.owner() == cell.id()) ? *oppositeFace.neighbor() : oppositeFace.owner();

  const Real phiP = field[cell.id()];
  const Real phiB = scalarBc->boundaryValue(phiP, h1);
  const Real phiN = field[farCellId];

  // Three-point one-sided derivative at x1=P for points (B, P, N) at
  // relative positions (0, h1, h1+h2): exact for quadratics, O(h^2) in
  // general -- see docs/architecture or TODO.md P0 gate notes.
  const Real a = -h2 / (h1 * (h1 + h2));
  const Real b = (h2 - h1) / (h1 * h2);
  const Real c = h1 / (h2 * (h1 + h2));
  const Real dPhiDInward = (a * phiB) + (b * phiP) + (c * phiN);

  // grad along outward normal = -dPhiDInward. The caller divides the
  // whole per-cell sum by cell.volume() exactly once at the end (summed
  // together with every other face's contribution), so what we hand
  // back here must already be "would-be gradient * cell.volume()" --
  // NOT scaled by face area or by h1+h2, which are unrelated to the
  // cell's actual volume.
  const Vector2 outwardNormal = MeshGeometry::unitNormal(boundaryFace);
  const Vector2 contribution = outwardNormal * (-dPhiDInward * cell.volume());

  return {true, *oppositeFaceId, contribution};
}

}  // namespace

// --- P12-NUM-002: weighted least-squares gradient reconstruction. -------

LeastSquaresGradientResult solveLeastSquaresGradient(const std::vector<Vector2>& displacements,
                                                     const std::vector<Real>& valueDifferences) {
  if (displacements.size() != valueDifferences.size()) {
    throw InvalidArgumentError(
        "solveLeastSquaresGradient: displacements and valueDifferences must be the same size");
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

// One Green-Gauss summation over the given face values -- exactly the
// pre-P12-NUM-003 per-cell loop (paired boundary refinement included),
// factored out so the skewness-corrected gradient below can re-run it.
VectorField greenGaussSweep(const Mesh& mesh, const ScalarField& field,
                            const BoundaryConditionSet& boundaries,
                            const SurfaceField& faceValues) {
  VectorField result(mesh.numberOfCells(), Vector2{0.0, 0.0});
  for (const auto& cell : mesh.cells()) {
    Vector2 sum{0.0, 0.0};

    // First pass: pair up every boundary face of this cell with its
    // opposite interior neighbor (if any) up front, before any standard
    // per-face contribution is added. Doing this in a separate pass --
    // rather than inline, in cell.faceIds() order -- avoids double-
    // counting the opposite face: face insertion order depends on which
    // side of the cell the boundary sits on (see MeshGeometry's
    // structured generator), so the opposite face is not reliably
    // visited *after* its boundary face in a single pass.
    std::vector<Index> handledFaceIds;
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      if (!face.isBoundary()) {
        continue;
      }
      const PairedBoundaryContribution paired =
          tryPairedBoundaryContribution(mesh, cell, face, field, boundaries);
      if (paired.applies) {
        sum += paired.contribution;
        handledFaceIds.push_back(faceId);
        handledFaceIds.push_back(paired.oppositeFaceId);
      }
    }

    // Second pass: standard treatment for every face not already folded
    // into a paired contribution above.
    for (const Index faceId : cell.faceIds()) {
      if (std::find(handledFaceIds.begin(), handledFaceIds.end(), faceId) != handledFaceIds.end()) {
        continue;
      }
      const auto& face = mesh.face(faceId);
      const Vector2 sfCell =
          (face.owner() == cell.id()) ? face.areaVector() : (face.areaVector() * -1.0);
      sum += sfCell * faceValues[faceId];
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
  VectorField result = greenGaussSweep(mesh, field, boundaries, faceValues);

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
    if (crossing.has_value() && (crossing->skewVector.x != 0.0 || crossing->skewVector.y != 0.0)) {
      skewedFaces.push_back(face.id());
    }
  }
  for (Index sweep = 0; sweep < skewCorrectionSweeps && !skewedFaces.empty(); ++sweep) {
    for (const Index faceId : skewedFaces) {
      faceValues[faceId] =
          interpolateInternalFaceSkewCorrected(mesh, mesh.face(faceId), field, result);
    }
    result = greenGaussSweep(mesh, field, boundaries, faceValues);
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

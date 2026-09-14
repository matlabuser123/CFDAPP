#include "cfd/mesh/MeshGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::mesh {

Real MeshGeometry::distance(const Vector2& a, const Vector2& b) noexcept {
  return magnitude(b - a);
}

Vector2 MeshGeometry::displacement(const Vector2& from, const Vector2& to) noexcept {
  return to - from;
}

Vector2 MeshGeometry::unitNormal(const Face& face) {
  const Real area = face.area();
  if (!(area > 0.0)) {
    throw NumericalError("Cannot compute the unit normal of a zero-area face");
  }
  return face.areaVector() * (1.0 / area);
}

Real MeshGeometry::ownerNeighborDistance(const Mesh& mesh, const Face& face) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("ownerNeighborDistance requires an internal face");
  }
  const Vector2& ownerCentroid = mesh.cell(face.owner()).centroid();
  const Vector2& neighborCentroid = mesh.cell(*face.neighbor()).centroid();
  return distance(ownerCentroid, neighborCentroid);
}

std::optional<Index> MeshGeometry::oppositeInteriorFace(const Mesh& mesh, const Cell& cell,
                                                        const Face& referenceFace) {
  // Orient referenceFace's own normal outward from `cell` the same way
  // every candidate below is oriented -- for a boundary face this is a
  // no-op (its only cell is always the owner, so rawReferenceNormal is
  // already outward from it), which is why every pre-existing (boundary-
  // face) caller sees unchanged behavior; for an internal reference face
  // where `cell` is the NEIGHBOR, this flips the sign the raw stored
  // Sf-direction would otherwise give (see this function's own header
  // comment).
  const Vector2 rawReferenceNormal = unitNormal(referenceFace);
  const Vector2 outwardReferenceNormal =
      (referenceFace.owner() == cell.id()) ? rawReferenceNormal : (rawReferenceNormal * -1.0);

  // P12-NUM-001 bugfix: a candidate only counts as "opposite" if it is
  // genuinely anti-parallel (alignment < 0) to referenceFace -- a merely
  // PERPENDICULAR face (alignment == 0, e.g. a y-direction face when
  // referenceFace is an x-direction one) must never win by default. The
  // previous `!best.has_value() || alignment < bestDot` (bestDot seeded
  // at 0.0) accepted whatever internal candidate was enumerated first
  // regardless of its alignment, silently returning a perpendicular face
  // as if it were "opposite" whenever no genuinely-opposite interior
  // face existed (e.g. a boundary-adjacent cell whose only other
  // interior faces run perpendicular to referenceFace's own axis).
  // Every pre-existing (Gradient.cpp) caller is unaffected: on every
  // mesh/grid size that code is ever exercised against, a genuinely
  // anti-parallel (alignment ~= -1, exact on this orthogonal Cartesian
  // mesh) candidate always exists and always wins this comparison
  // regardless of the acceptance threshold -- confirmed by the full
  // regression suite passing unchanged after this fix. This was
  // discovered via P12-NUM-001's QUICK far-upstream-cell lookup, which
  // legitimately hits the previously-mishandled case (see
  // QuickDegradesToUpwindWhenNoFartherUpstreamCellExists and
  // LinearFieldIsReproducedExactlyByEveryScheme in test_convection.cpp).
  std::optional<Index> best;
  Real bestDot = 0.0;
  for (const Index faceId : cell.faceIds()) {
    if (faceId == referenceFace.id()) {
      continue;
    }
    const Face& candidate = mesh.face(faceId);
    if (candidate.isBoundary()) {
      continue;
    }
    const Vector2 rawNormal = unitNormal(candidate);
    const Vector2 outwardFromCell =
        (candidate.owner() == cell.id()) ? rawNormal : (rawNormal * -1.0);
    const Real alignment = dot(outwardReferenceNormal, outwardFromCell);
    if (alignment < 0.0 && (!best.has_value() || alignment < bestDot)) {
      bestDot = alignment;
      best = faceId;
    }
  }
  return best;
}

namespace {

// Shared degeneracy guard for decomposeFaceArea/nonOrthogonalityAngleDegrees/
// skewness: `d . Sf` must be safely bounded away from zero relative to
// |d||Sf| (a scale-invariant, dimensionless threshold -- both sides are
// already-normalized cosines-like quantities once divided through, but
// computing it this way avoids two separate sqrt calls). Threshold
// 1e-6 corresponds to an angle of ~89.9994 degrees between Sf and d --
// permissive enough to accept genuinely poor (but not mathematically
// degenerate) meshes, while still rejecting the >=90-degree case where
// the over-relaxed decomposition's own denominator changes sign or
// vanishes.
bool isFaceGeometryWellPosed(const Vector2& d, const Vector2& sf) noexcept {
  const Real dDotSf = dot(d, sf);
  const Real dMag = magnitude(d);
  const Real sfMag = magnitude(sf);
  if (!(dMag > 0.0) || !(sfMag > 0.0)) {
    return false;
  }
  return dDotSf > (1e-6 * dMag * sfMag);
}

}  // namespace

MeshGeometry::NonOrthogonalDecomposition MeshGeometry::decomposeFaceArea(const Mesh& mesh,
                                                                         const Face& face) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("decomposeFaceArea requires an internal face");
  }
  return decomposeAreaVector(
      mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid(),
      face.areaVector());
}

MeshGeometry::NonOrthogonalDecomposition MeshGeometry::decomposeBoundaryFaceArea(const Mesh& mesh,
                                                                                 const Face& face) {
  if (!face.isBoundary()) {
    throw InvalidArgumentError("decomposeBoundaryFaceArea requires a boundary face");
  }
  return decomposeAreaVector(face.centroid() - mesh.cell(face.owner()).centroid(),
                             face.areaVector());
}

MeshGeometry::NonOrthogonalDecomposition MeshGeometry::decomposeAreaVector(
    const Vector2& d, const Vector2& sf) noexcept {
  if (!isFaceGeometryWellPosed(d, sf)) {
    return {};  // {0,0}, {0,0}, valid=false -- see this struct's own header comment.
  }

  // Exactly parallel (the 2D cross product is exactly zero -- true for
  // every internal face of MeshGeometry::createCartesian2D, where d and
  // Sf are both exactly axis-aligned): the over-relaxed formula's exact
  // value is S_orth == Sf, S_nonorth == {0,0}. Returned directly rather
  // than through the general formula below, whose floating-point
  // evaluation (d * (Sf.Sf / d.Sf)) can land 1 ulp away from Sf -- this
  // makes the documented "exactly zero on an orthogonal face" guarantee
  // hold bit-for-bit, so enabling the correction on a Cartesian mesh
  // reproduces the uncorrected operator exactly, not merely to round-off.
  // Not a special case in the mathematics: the general formula's limit
  // as the cross product -> 0 is this same value.
  if ((d.x * sf.y) - (d.y * sf.x) == 0.0) {
    return {sf, Vector2{0.0, 0.0}, true};
  }

  // Over-relaxed approach: S_orth = (Sf.Sf / d.Sf) * d -- see this
  // function's own header comment for the derivation.
  const Real sfDotSf = dot(sf, sf);
  const Real dDotSf = dot(d, sf);
  const Vector2 orthogonal = d * (sfDotSf / dDotSf);
  const Vector2 nonOrthogonal = sf - orthogonal;
  return {orthogonal, nonOrthogonal, true};
}

Real MeshGeometry::nonOrthogonalityAngleDegrees(const Mesh& mesh, const Face& face) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("nonOrthogonalityAngleDegrees requires an internal face");
  }
  const Vector2 d = mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid();
  const Vector2& sf = face.areaVector();
  const Real dMag = magnitude(d);
  const Real sfMag = magnitude(sf);
  if (!(dMag > 0.0) || !(sfMag > 0.0)) {
    throw NumericalError(
        "nonOrthogonalityAngleDegrees: owner-neighbor distance or face area is zero");
  }
  // Clamp guards only against floating-point round-off pushing the ratio
  // fractionally outside [-1, 1] (e.g. 1.0000000000000002) -- acos of
  // anything genuinely outside that range would already indicate |d| or
  // |Sf| was computed inconsistently, not a case to silently paper over
  // further.
  const Real cosTheta = std::clamp(dot(d, sf) / (dMag * sfMag), -1.0, 1.0);
  return std::acos(cosTheta) * (180.0 / constants::pi);
}

std::optional<MeshGeometry::FaceCrossing> MeshGeometry::ownerNeighborCrossing(const Mesh& mesh,
                                                                              const Face& face) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("ownerNeighborCrossing requires an internal face");
  }
  const Vector2& ownerCentroid = mesh.cell(face.owner()).centroid();
  const Vector2 d = mesh.cell(*face.neighbor()).centroid() - ownerCentroid;
  const Vector2& sf = face.areaVector();

  // Intersection of the line x(t) = ownerCentroid + t*d with the face's
  // own line (through face.centroid(), normal sf): solve
  // sf.(x(t) - face.centroid()) = 0 for t -- see this struct's own
  // header comment.
  if (!isFaceGeometryWellPosed(d, sf)) {
    return std::nullopt;
  }
  // Face centroid EXACTLY on the owner-neighbor line (2D cross product
  // exactly zero -- every face of createCartesian2D): the crossing point is
  // the face centroid itself and the skew vector is exactly {0,0}. Returned
  // directly because the general formula below can land 1 ulp off, which
  // would make "unskewed" faces look minutely skewed and break the
  // bit-identity of skew-corrected operators on orthogonal meshes.
  const Vector2 toFace = face.centroid() - ownerCentroid;
  if ((toFace.x * d.y) - (toFace.y * d.x) == 0.0) {
    return FaceCrossing{dot(toFace, d) / dot(d, d), face.centroid(), Vector2{0.0, 0.0}};
  }
  const Real t = dot(face.centroid() - ownerCentroid, sf) / dot(d, sf);
  const Vector2 crossingPoint = ownerCentroid + (d * t);
  return FaceCrossing{t, crossingPoint, face.centroid() - crossingPoint};
}

std::optional<Real> MeshGeometry::skewness(const Mesh& mesh, const Face& face) {
  if (face.isBoundary()) {
    throw InvalidArgumentError("skewness requires an internal face");
  }
  const auto crossing = ownerNeighborCrossing(mesh, face);
  if (!crossing.has_value()) {
    return std::nullopt;
  }
  return magnitude(crossing->skewVector) / ownerNeighborDistance(mesh, face);
}

namespace {

void validateCartesianInputs(Index nx, Index ny, Real lengthX, Real lengthY) {
  if (nx == 0) {
    throw InvalidArgumentError("Cartesian mesh requires nx > 0");
  }
  if (ny == 0) {
    throw InvalidArgumentError("Cartesian mesh requires ny > 0");
  }
  if (!std::isfinite(lengthX) || !(lengthX > 0.0)) {
    throw InvalidArgumentError("Cartesian mesh requires a finite, positive lengthX");
  }
  if (!std::isfinite(lengthY) || !(lengthY > 0.0)) {
    throw InvalidArgumentError("Cartesian mesh requires a finite, positive lengthY");
  }
}

}  // namespace

Mesh MeshGeometry::createCartesian2D(Index nx, Index ny, Real lengthX, Real lengthY) {
  validateCartesianInputs(nx, ny, lengthX, lengthY);

  const Real dx = lengthX / static_cast<Real>(nx);
  const Real dy = lengthY / static_cast<Real>(ny);
  const Real cellVolume = dx * dy;

  const auto cellIndex = [nx](Index i, Index j) noexcept -> Index { return (j * nx) + i; };

  // --- Cells ----------------------------------------------------------
  std::vector<Cell> cells;
  cells.reserve(nx * ny);
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Real xP = (static_cast<Real>(i) + 0.5) * dx;
      const Real yP = (static_cast<Real>(j) + 0.5) * dy;
      cells.emplace_back(cellIndex(i, j), Vector2{xP, yP}, cellVolume);
    }
  }

  std::vector<Face> faces;
  faces.reserve((2 * nx * ny) + nx + ny);

  std::vector<Index> leftFaceIds;
  std::vector<Index> rightFaceIds;
  std::vector<Index> bottomFaceIds;
  std::vector<Index> topFaceIds;
  leftFaceIds.reserve(ny);
  rightFaceIds.reserve(ny);
  bottomFaceIds.reserve(nx);
  topFaceIds.reserve(nx);

  Index nextFaceId = 0;

  // --- Vertical faces (normal along x), column i in [0, nx] -----------
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Vector2 centroid{static_cast<Real>(i) * dx, (static_cast<Real>(j) + 0.5) * dy};
      const Index faceId = nextFaceId++;

      if (i == 0) {
        const Index owner = cellIndex(0, j);
        faces.emplace_back(faceId, owner, std::nullopt, centroid, Vector2{-dy, 0.0});
        leftFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else if (i == nx) {
        const Index owner = cellIndex(nx - 1, j);
        faces.emplace_back(faceId, owner, std::nullopt, centroid, Vector2{dy, 0.0});
        rightFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else {
        const Index owner = cellIndex(i - 1, j);
        const Index neighbor = cellIndex(i, j);
        faces.emplace_back(faceId, owner, neighbor, centroid, Vector2{dy, 0.0});
        cells[owner].addFace(faceId);
        cells[neighbor].addFace(faceId);
      }
    }
  }

  // --- Horizontal faces (normal along y), row j in [0, ny] -------------
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Vector2 centroid{(static_cast<Real>(i) + 0.5) * dx, static_cast<Real>(j) * dy};
      const Index faceId = nextFaceId++;

      if (j == 0) {
        const Index owner = cellIndex(i, 0);
        faces.emplace_back(faceId, owner, std::nullopt, centroid, Vector2{0.0, -dx});
        bottomFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else if (j == ny) {
        const Index owner = cellIndex(i, ny - 1);
        faces.emplace_back(faceId, owner, std::nullopt, centroid, Vector2{0.0, dx});
        topFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else {
        const Index owner = cellIndex(i, j - 1);
        const Index neighbor = cellIndex(i, j);
        faces.emplace_back(faceId, owner, neighbor, centroid, Vector2{0.0, dx});
        cells[owner].addFace(faceId);
        cells[neighbor].addFace(faceId);
      }
    }
  }

  std::vector<BoundaryPatch> patches;
  patches.reserve(4);
  patches.emplace_back("left", std::move(leftFaceIds));
  patches.emplace_back("right", std::move(rightFaceIds));
  patches.emplace_back("bottom", std::move(bottomFaceIds));
  patches.emplace_back("top", std::move(topFaceIds));

  return Mesh(std::move(cells), std::move(faces), std::move(patches));
}

}  // namespace cfd::mesh

#include "cfd/mesh/MeshGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
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

  // Exactly parallel (the cross product is exactly zero -- true for every
  // internal face of MeshGeometry::createCartesian2D/createCartesian3D,
  // where d and Sf are both exactly axis-aligned): the over-relaxed
  // formula's exact value is S_orth == Sf, S_nonorth == {0,0}. Returned
  // directly rather than through the general formula below, whose
  // floating-point evaluation (d * (Sf.Sf / d.Sf)) can land 1 ulp away
  // from Sf -- this makes the documented "exactly zero on an orthogonal
  // face" guarantee hold bit-for-bit, so enabling the correction on a
  // Cartesian mesh reproduces the uncorrected operator exactly, not merely
  // to round-off. Not a special case in the mathematics: the general
  // formula's limit as the cross product -> 0 is this same value.
  // (P12-MESH-005: the full 3D cross product; for 2D vectors its only
  // non-zero component is the former 2D formula, so the test is unchanged.)
  if (cross(d, sf) == Vector3{}) {
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
  // Face centroid EXACTLY on the owner-neighbor line (cross product
  // exactly zero -- every face of createCartesian2D/createCartesian3D): the
  // crossing point is the face centroid itself and the skew vector is
  // exactly {0,0}. Returned directly because the general formula below can
  // land 1 ulp off, which would make "unskewed" faces look minutely skewed
  // and break the bit-identity of skew-corrected operators on orthogonal
  // meshes. (P12-MESH-005: full 3D cross product, same test in 2D.)
  const Vector2 toFace = face.centroid() - ownerCentroid;
  if (cross(toFace, d) == Vector3{}) {
    return FaceCrossing{dot(toFace, d) / dot(d, d), face.centroid(), Vector2{0.0, 0.0}};
  }
  const Real t = dot(face.centroid() - ownerCentroid, sf) / dot(d, sf);
  const Vector2 crossingPoint = ownerCentroid + (d * t);
  return FaceCrossing{t, crossingPoint, face.centroid() - crossingPoint};
}

MeshGeometry::BoundaryLineIntersection MeshGeometry::boundaryLineIntersection(
    const Mesh& mesh, const Face& boundaryFace, const Vector2& direction) {
  if (!boundaryFace.isBoundary()) {
    throw InvalidArgumentError("boundaryLineIntersection requires a boundary face");
  }
  // P12-GRAD-002 -- see the header comment. `direction` points from the owner
  // into the mesh, so it opposes the outward normal and both dot products below
  // are negative on a valid cell; their ratio is the positive distance from the
  // owner centroid back to the boundary. The guard is the same well-posedness
  // test the other geometry functions use, applied to the inward line rather
  // than to the owner-neighbor line, so a face the line runs parallel to is
  // rejected instead of producing an unbounded t.
  const Vector2& ownerCentroid = mesh.cell(boundaryFace.owner()).centroid();
  const Vector2& sf = boundaryFace.areaVector();
  if (!isFaceGeometryWellPosed(direction * -1.0, sf)) {
    return {};  // line parallel to (or leaving through) the face -- degenerate cell.
  }
  const Real distance = dot(ownerCentroid - boundaryFace.centroid(), sf) / dot(direction, sf);
  if (!(distance > 0.0)) {
    return {};  // boundary not behind the owner along this direction -- degenerate cell.
  }
  return {distance, ownerCentroid - (direction * distance), true};
}

MeshGeometry::BoundaryInwardStencil MeshGeometry::boundaryInwardStencil(const Mesh& mesh,
                                                                        const Face& boundaryFace) {
  if (!boundaryFace.isBoundary()) {
    throw InvalidArgumentError("boundaryInwardStencil requires a boundary face");
  }
  // P12-DIFF-002 -- see the header comment for the definitions and the validity conditions.
  BoundaryInwardStencil stencil;
  const Cell& owner = mesh.cell(boundaryFace.owner());
  const auto oppositeFaceId = oppositeInteriorFace(mesh, owner, boundaryFace);
  if (!oppositeFaceId.has_value()) {
    return stencil;  // topological: no interior face across the cell.
  }
  const Face& oppositeFace = mesh.face(*oppositeFaceId);
  const Index farCellId =
      (oppositeFace.owner() == owner.id()) ? *oppositeFace.neighbor() : oppositeFace.owner();
  const Vector2 normal = unitNormal(boundaryFace);
  const Vector2& faceCentroid = boundaryFace.centroid();
  const Real h1 = dot(faceCentroid - owner.centroid(), normal);
  const Real h2 = dot(faceCentroid - mesh.cell(farCellId).centroid(), normal);
  if (!(h1 > 0.0) || !(h2 > h1)) {
    return stencil;  // degenerate cell -- rejected by MeshQuality before any solve.
  }
  stencil.h1 = h1;
  stencil.h2 = h2;
  stencil.farCell = farCellId;
  // x_f - h1 n is the point on the inward ray at the owner's own normal distance; the remainder is
  // purely tangential, and exactly {0,0} on an orthogonal face.
  stencil.deltaP = owner.centroid() - (faceCentroid - (normal * h1));
  stencil.deltaF = mesh.cell(farCellId).centroid() - (faceCentroid - (normal * h2));
  stencil.valid = true;
  return stencil;
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

namespace {

struct CellGeometry {
  Vector2 centroid;
  Real volume;
};

struct FaceGeometry {
  Vector2 centroid;
  Vector2 areaVector;  // as stored: owner -> neighbor, or out of the domain
};

// The geometry of one structured quadrilateral cell (corners in
// counter-clockwise order vertex(i,j), (i+1,j), (i+1,j+1), (i,j+1)):
// shoelace area and centroid, or nullopt if the quad is not strictly convex
// and counter-clockwise (each of its four corner turns strictly positive --
// rules out folded, self-intersecting, clockwise, zero-area and
// degenerate-edge cells). Shared by createStructuredQuad2D (P12-MESH-001)
// and createMultiBlock2D (P12-MESH-003).
std::optional<CellGeometry> quadCellGeometry(const Vector2 (&corners)[4]) {
  for (int k = 0; k < 4; ++k) {
    const Vector2 e1 = corners[(k + 1) % 4] - corners[k];
    const Vector2 e2 = corners[(k + 2) % 4] - corners[(k + 1) % 4];
    if (!((e1.x * e2.y) - (e1.y * e2.x) > 0.0)) {
      return std::nullopt;
    }
  }
  Real signedArea = 0.0;
  Real cx = 0.0;
  Real cy = 0.0;
  for (int k = 0; k < 4; ++k) {
    const Vector2& a = corners[k];
    const Vector2& b = corners[(k + 1) % 4];
    const Real cross = (a.x * b.y) - (b.x * a.y);
    signedArea += cross;
    cx += (a.x + b.x) * cross;
    cy += (a.y + b.y) * cross;
  }
  signedArea *= 0.5;
  cx /= (6.0 * signedArea);
  cy /= (6.0 * signedArea);
  return CellGeometry{Vector2{cx, cy}, signedArea};
}

// P12-MESH-004: names the defect of a quad quadCellGeometry rejected, in
// the order a reader fixes them: a zero-length edge, zero area, clockwise
// (inverted) orientation, otherwise a non-convex (folded or
// self-intersecting) corner. Only builds the error message.
std::string quadCellDefect(const Vector2 (&corners)[4]) {
  static const char* const kCornerNames[4] = {"(i,j)", "(i+1,j)", "(i+1,j+1)", "(i,j+1)"};
  Real longestEdge = 0.0;
  for (int k = 0; k < 4; ++k) {
    const Real length = magnitude(corners[(k + 1) % 4] - corners[k]);
    if (!(length > 0.0)) {
      return std::string("degenerate edge: corners ") + kCornerNames[k] + " and " +
             kCornerNames[(k + 1) % 4] + " coincide (zero-length face)";
    }
    longestEdge = std::max(longestEdge, length);
  }
  Real signedArea = 0.0;
  for (int k = 0; k < 4; ++k) {
    const Vector2& a = corners[k];
    const Vector2& b = corners[(k + 1) % 4];
    signedArea += (a.x * b.y) - (b.x * a.y);
  }
  signedArea *= 0.5;
  // Round-off of the shoelace sum for coordinates of this size.
  const Real areaNoise = 16.0 * std::numeric_limits<Real>::epsilon() * longestEdge * longestEdge;
  if (std::abs(signedArea) <= areaNoise) return "zero area (collapsed cell)";
  if (signedArea < 0.0) return "inverted (clockwise corner order, negative area)";
  for (int k = 0; k < 4; ++k) {
    const Vector2 e1 = corners[(k + 1) % 4] - corners[k];
    const Vector2 e2 = corners[(k + 2) % 4] - corners[(k + 1) % 4];
    if (!((e1.x * e2.y) - (e1.y * e2.x) > 0.0)) {
      return std::string("not convex at corner ") + kCornerNames[(k + 1) % 4] +
             " (folded or self-intersecting)";
    }
  }
  return "not strictly convex";
}

// Face of the edge a -> b (a = vertex(i, j), b = vertex(i, j + 1)): area
// vector toward +i, negated when it must point the other way (the left
// boundary of a block).
FaceGeometry verticalEdgeFace(const Vector2& a, const Vector2& b, bool negate) {
  const Vector2 edge = b - a;
  Vector2 areaVector{edge.y, -edge.x};
  if (negate) areaVector = areaVector * -1.0;
  return FaceGeometry{(a + b) * 0.5, areaVector};
}

// Face of the edge a -> b (a = vertex(i, j), b = vertex(i + 1, j)): area
// vector toward +j, negated on the bottom boundary of a block.
FaceGeometry horizontalEdgeFace(const Vector2& a, const Vector2& b, bool negate) {
  const Vector2 edge = b - a;
  Vector2 areaVector{-edge.y, edge.x};
  if (negate) areaVector = areaVector * -1.0;
  return FaceGeometry{(a + b) * 0.5, areaVector};
}

// The one structured 2D topology every production mesh uses (P12-MESH-002
// refactor of what createCartesian2D and createStructuredQuad2D each built
// separately): cell(i, j) = j * nx + i; vertical faces first (j outer,
// i = 0..nx: i = 0 "left" boundary, i = nx "right", owner cell(i-1, j) /
// neighbor cell(i, j) otherwise), then horizontal faces (j = 0..ny outer,
// i inner: "bottom", "top", owner cell(i, j-1) / neighbor cell(i, j)); each
// cell lists its faces in that creation order. Geometry comes from the
// three callbacks, evaluated in exactly that order: cellGeometry(i, j),
// verticalFace(i, j) and horizontalFace(i, j) return the final stored
// values (boundary orientation included), so each builder keeps its own
// arithmetic bit for bit.
template <class CellFn, class VerticalFn, class HorizontalFn>
Mesh buildStructuredMesh(Index nx, Index ny, CellFn cellGeometry, VerticalFn verticalFace,
                         HorizontalFn horizontalFace, StructuredGrid grid) {
  const auto cellIndex = [nx](Index i, Index j) noexcept -> Index { return (j * nx) + i; };

  std::vector<Cell> cells;
  cells.reserve(nx * ny);
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const CellGeometry g = cellGeometry(i, j);
      cells.emplace_back(cellIndex(i, j), g.centroid, g.volume);
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

  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const FaceGeometry g = verticalFace(i, j);
      const Index faceId = nextFaceId++;
      if (i == 0) {
        const Index owner = cellIndex(0, j);
        faces.emplace_back(faceId, owner, std::nullopt, g.centroid, g.areaVector);
        leftFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else if (i == nx) {
        const Index owner = cellIndex(nx - 1, j);
        faces.emplace_back(faceId, owner, std::nullopt, g.centroid, g.areaVector);
        rightFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else {
        const Index owner = cellIndex(i - 1, j);
        const Index neighbor = cellIndex(i, j);
        faces.emplace_back(faceId, owner, neighbor, g.centroid, g.areaVector);
        cells[owner].addFace(faceId);
        cells[neighbor].addFace(faceId);
      }
    }
  }

  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const FaceGeometry g = horizontalFace(i, j);
      const Index faceId = nextFaceId++;
      if (j == 0) {
        const Index owner = cellIndex(i, 0);
        faces.emplace_back(faceId, owner, std::nullopt, g.centroid, g.areaVector);
        bottomFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else if (j == ny) {
        const Index owner = cellIndex(i, ny - 1);
        faces.emplace_back(faceId, owner, std::nullopt, g.centroid, g.areaVector);
        topFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else {
        const Index owner = cellIndex(i, j - 1);
        const Index neighbor = cellIndex(i, j);
        faces.emplace_back(faceId, owner, neighbor, g.centroid, g.areaVector);
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

  return Mesh(std::move(cells), std::move(faces), std::move(patches), std::move(grid));
}

}  // namespace

Mesh MeshGeometry::createCartesian2D(Index nx, Index ny, Real lengthX, Real lengthY) {
  validateCartesianInputs(nx, ny, lengthX, lengthY);
  return createRectilinear2D(AxisSpacing::uniform(nx, lengthX), AxisSpacing::uniform(ny, lengthY));
}

Mesh MeshGeometry::createRectilinear2D(const AxisSpacing& x, const AxisSpacing& y) {
  const Index nx = x.cells();
  const Index ny = y.cells();
  if (nx == 0 || ny == 0) {
    throw InvalidArgumentError("createRectilinear2D: both axes need at least one cell");
  }

  // Axis-aligned rectangles: centroid = the cell's centre, volume =
  // width * height, faces at the nodes with area vectors (+-height, 0) /
  // (0, +-width). For uniform axes these are exactly the expressions of the
  // original createCartesian2D (AxisSpacing::uniform), so a Cartesian mesh
  // is bit-identical; every face is exactly orthogonal and unskewed.
  const auto cellGeometry = [&](Index i, Index j) {
    return CellGeometry{Vector2{x.center(i), y.center(j)}, x.width(i) * y.width(j)};
  };
  const auto verticalFace = [&](Index i, Index j) {
    const Real height = y.width(j);
    return FaceGeometry{Vector2{x.node(i), y.center(j)},
                        i == 0 ? Vector2{-height, 0.0} : Vector2{height, 0.0}};
  };
  const auto horizontalFace = [&](Index i, Index j) {
    const Real width = x.width(i);
    return FaceGeometry{Vector2{x.center(i), y.node(j)},
                        j == 0 ? Vector2{0.0, -width} : Vector2{0.0, width}};
  };

  // The generating vertex grid (export only; the numerics never read it).
  StructuredGrid grid{nx, ny, {}, {}};
  grid.vertices.reserve((nx + 1) * (ny + 1));
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      grid.vertices.push_back(Vector2{x.node(i), y.node(j)});
    }
  }
  return buildStructuredMesh(nx, ny, cellGeometry, verticalFace, horizontalFace, std::move(grid));
}

Mesh MeshGeometry::createGraded2D(Index nx, Index ny, Real lengthX, Real lengthY,
                                  const AxisGrading& xGrading, const AxisGrading& yGrading) {
  return createRectilinear2D(AxisSpacing::graded(nx, lengthX, xGrading),
                             AxisSpacing::graded(ny, lengthY, yGrading));
}

Mesh MeshGeometry::createStructuredQuad2D(Index nx, Index ny,
                                          const std::vector<Vector2>& vertices) {
  if (nx == 0 || ny == 0) {
    throw InvalidArgumentError("createStructuredQuad2D: nx and ny must be > 0");
  }
  if (vertices.size() != (nx + 1) * (ny + 1)) {
    throw InvalidArgumentError("createStructuredQuad2D: expected (nx + 1) * (ny + 1) = " +
                               std::to_string((nx + 1) * (ny + 1)) + " vertices, got " +
                               std::to_string(vertices.size()));
  }
  const auto vertexIndex = [nx](Index i, Index j) noexcept -> Index { return (j * (nx + 1)) + i; };
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Vector2& v = vertices[vertexIndex(i, j)];
      if (!std::isfinite(v.x) || !std::isfinite(v.y)) {
        throw InvalidArgumentError("createStructuredQuad2D: vertex (" + std::to_string(i) + "," +
                                   std::to_string(j) + ") has a non-finite coordinate");
      }
    }
  }

  // Cells: strictly convex, counter-clockwise quads; shoelace geometry.
  const auto cellGeometry = [&](Index i, Index j) {
    const Vector2 corners[4] = {vertices[vertexIndex(i, j)], vertices[vertexIndex(i + 1, j)],
                                vertices[vertexIndex(i + 1, j + 1)],
                                vertices[vertexIndex(i, j + 1)]};
    const std::optional<CellGeometry> g = quadCellGeometry(corners);
    if (!g.has_value()) {
      throw InvalidArgumentError(
          "createStructuredQuad2D: cell (" + std::to_string(i) + "," + std::to_string(j) +
          ") is not a strictly convex counter-clockwise quadrilateral: " + quadCellDefect(corners));
    }
    return *g;
  };

  // Vertical faces: edge vertex(i, j) -> vertex(i, j + 1); area vector
  // toward +i (negated on the left boundary to point out of the domain).
  const auto verticalFace = [&](Index i, Index j) {
    return verticalEdgeFace(vertices[vertexIndex(i, j)], vertices[vertexIndex(i, j + 1)], i == 0);
  };

  // Horizontal faces: edge vertex(i, j) -> vertex(i + 1, j); area vector
  // toward +j (negated on the bottom boundary).
  const auto horizontalFace = [&](Index i, Index j) {
    return horizontalEdgeFace(vertices[vertexIndex(i, j)], vertices[vertexIndex(i + 1, j)], j == 0);
  };

  return buildStructuredMesh(nx, ny, cellGeometry, verticalFace, horizontalFace,
                             StructuredGrid{nx, ny, vertices, {}});
}

// ---------------------------------------------------------------------------
// P12-MESH-003: conformal multi-block structured meshes
// ---------------------------------------------------------------------------

const char* blockSideName(BlockSide side) noexcept {
  switch (side) {
    case BlockSide::Left:
      return "left";
    case BlockSide::Right:
      return "right";
    case BlockSide::Bottom:
      return "bottom";
    case BlockSide::Top:
      return "top";
  }
  return "left";
}

namespace {

std::string describeSide(const MultiBlockSpec& spec, const BlockSideRef& ref) {
  return "block '" + spec.blocks[ref.block].name + "' side '" + blockSideName(ref.side) + "'";
}

std::string describePoint(const Vector2& p) {
  std::ostringstream out;
  out.precision(17);
  out << "(" << p.x << ", " << p.y << ")";
  return out.str();
}

Index sideFaceCount(const BlockSpec& block, BlockSide side) {
  return (side == BlockSide::Bottom || side == BlockSide::Top) ? block.nx : block.ny;
}

// Vertex k (0..faces) of a block side, traversed in increasing local index.
const Vector2& sideVertex(const BlockSpec& block, BlockSide side, Index k) {
  const Index stride = block.nx + 1;
  switch (side) {
    case BlockSide::Bottom:
      return block.vertices[k];
    case BlockSide::Top:
      return block.vertices[(block.ny * stride) + k];
    case BlockSide::Left:
      return block.vertices[k * stride];
    case BlockSide::Right:
      return block.vertices[(k * stride) + block.nx];
  }
  return block.vertices[0];
}

// Local cell (i, j) owning face k of a block side.
std::pair<Index, Index> sideCell(const BlockSpec& block, BlockSide side, Index k) {
  switch (side) {
    case BlockSide::Bottom:
      return {k, 0};
    case BlockSide::Top:
      return {k, block.ny - 1};
    case BlockSide::Left:
      return {0, k};
    case BlockSide::Right:
      return {block.nx - 1, k};
  }
  return {0, 0};
}

// Non-zero winding number of p with respect to oriented segments
// (Sunday's crossing rule; exact orientation arithmetic on doubles).
int windingNumber(const Vector2& p, const std::vector<std::pair<Vector2, Vector2>>& segments) {
  int winding = 0;
  for (const auto& [a, b] : segments) {
    const Real isLeft = ((b.x - a.x) * (p.y - a.y)) - ((p.x - a.x) * (b.y - a.y));
    if (a.y <= p.y) {
      if (b.y > p.y && isLeft > 0.0) ++winding;
    } else if (b.y <= p.y && isLeft < 0.0) {
      --winding;
    }
  }
  return winding;
}

}  // namespace

Mesh MeshGeometry::createMultiBlock2D(const MultiBlockSpec& spec) {
  const auto fail = [](const std::string& message) {
    throw InvalidArgumentError("multi-block mesh: " + message);
  };
  if (spec.blocks.empty()) fail("at least one block is required");

  // --- Blocks: dimensions, vertices, cells ---------------------------------
  std::vector<Index> cellOffset(spec.blocks.size() + 1, 0);
  for (std::size_t b = 0; b < spec.blocks.size(); ++b) {
    const BlockSpec& block = spec.blocks[b];
    if (block.name.empty()) fail("block " + std::to_string(b) + " has no name");
    for (std::size_t o = 0; o < b; ++o) {
      if (spec.blocks[o].name == block.name) fail("duplicate block name '" + block.name + "'");
    }
    if (block.nx == 0 || block.ny == 0) fail("block '" + block.name + "' needs nx, ny > 0");
    if (block.vertices.size() != (block.nx + 1) * (block.ny + 1)) {
      fail("block '" + block.name +
           "' needs (nx + 1) * (ny + 1) = " + std::to_string((block.nx + 1) * (block.ny + 1)) +
           " vertices, got " + std::to_string(block.vertices.size()));
    }
    for (std::size_t k = 0; k < block.vertices.size(); ++k) {
      if (!std::isfinite(block.vertices[k].x) || !std::isfinite(block.vertices[k].y)) {
        fail("block '" + block.name + "' vertex " + std::to_string(k) +
             " has a non-finite coordinate");
      }
    }
    cellOffset[b + 1] = cellOffset[b] + (block.nx * block.ny);
  }

  // --- Side usage: every side exactly once, in one interface or one patch --
  std::vector<std::array<int, 4>> sideUse(spec.blocks.size(), {0, 0, 0, 0});
  const auto useSide = [&](const BlockSideRef& ref, const std::string& where) {
    if (ref.block >= spec.blocks.size()) fail(where + " refers to a non-existent block");
    int& uses = sideUse[ref.block][static_cast<int>(ref.side)];
    if (++uses > 1) {
      fail(describeSide(spec, ref) + " is used more than once (" + where +
           "); each block side is one interface or belongs to one patch");
    }
  };
  for (std::size_t n = 0; n < spec.interfaces.size(); ++n) {
    const auto& iface = spec.interfaces[n];
    useSide(iface.first, "interface " + std::to_string(n));
    useSide(iface.second, "interface " + std::to_string(n));
  }
  for (std::size_t p = 0; p < spec.patches.size(); ++p) {
    const auto& patch = spec.patches[p];
    if (patch.name.empty()) fail("boundary patch " + std::to_string(p) + " has no name");
    for (std::size_t o = 0; o < p; ++o) {
      if (spec.patches[o].name == patch.name) fail("duplicate patch name '" + patch.name + "'");
    }
    if (patch.sides.empty()) fail("patch '" + patch.name + "' has no block sides");
    for (const auto& ref : patch.sides) useSide(ref, "patch '" + patch.name + "'");
  }
  for (std::size_t b = 0; b < spec.blocks.size(); ++b) {
    for (const BlockSide side :
         {BlockSide::Left, BlockSide::Right, BlockSide::Bottom, BlockSide::Top}) {
      if (sideUse[b][static_cast<int>(side)] == 0) {
        fail(describeSide(spec, BlockSideRef{b, side}) +
             " is neither an interface nor part of a boundary patch");
      }
    }
  }

  // --- Interfaces: equal face counts, identical vertices --------------------
  // partner[block][side] = (interface index, is-first-side) for interface sides.
  std::vector<std::array<std::optional<std::pair<std::size_t, bool>>, 4>> partner(
      spec.blocks.size());
  for (std::size_t n = 0; n < spec.interfaces.size(); ++n) {
    const auto& iface = spec.interfaces[n];
    const BlockSpec& a = spec.blocks[iface.first.block];
    const BlockSpec& b = spec.blocks[iface.second.block];
    const Index faces = sideFaceCount(a, iface.first.side);
    if (sideFaceCount(b, iface.second.side) != faces) {
      fail("interface " + std::to_string(n) + ": " + describeSide(spec, iface.first) + " has " +
           std::to_string(faces) + " faces but " + describeSide(spec, iface.second) + " has " +
           std::to_string(sideFaceCount(b, iface.second.side)) + " (interfaces must be conformal)");
    }
    for (Index k = 0; k <= faces; ++k) {
      const Vector2& p = sideVertex(a, iface.first.side, k);
      const Vector2& q = sideVertex(b, iface.second.side, iface.reversed ? faces - k : k);
      if (!(p.x == q.x && p.y == q.y)) {
        fail("interface " + std::to_string(n) + ": vertex " + std::to_string(k) + " of " +
             describeSide(spec, iface.first) + " " + describePoint(p) + " does not coincide with " +
             describeSide(spec, iface.second) + " vertex " +
             std::to_string(iface.reversed ? faces - k : k) + " " + describePoint(q) +
             " (interface vertices must be identical; orientation " +
             (iface.reversed ? "reversed" : "aligned") + ")");
      }
    }
    partner[iface.first.block][static_cast<int>(iface.first.side)] = std::make_pair(n, true);
    partner[iface.second.block][static_cast<int>(iface.second.side)] = std::make_pair(n, false);
  }

  // --- Cells ------------------------------------------------------------------
  std::vector<Cell> cells;
  cells.reserve(cellOffset.back());
  for (std::size_t b = 0; b < spec.blocks.size(); ++b) {
    const BlockSpec& block = spec.blocks[b];
    const Index stride = block.nx + 1;
    for (Index j = 0; j < block.ny; ++j) {
      for (Index i = 0; i < block.nx; ++i) {
        const Vector2 corners[4] = {
            block.vertices[(j * stride) + i], block.vertices[(j * stride) + i + 1],
            block.vertices[((j + 1) * stride) + i + 1], block.vertices[((j + 1) * stride) + i]};
        const std::optional<CellGeometry> g = quadCellGeometry(corners);
        if (!g.has_value()) {
          fail("block '" + block.name + "' cell (" + std::to_string(i) + "," + std::to_string(j) +
               ") is not a strictly convex counter-clockwise quadrilateral: " +
               quadCellDefect(corners));
        }
        cells.emplace_back(cellOffset[b] + (j * block.nx) + i, g->centroid, g->volume);
      }
    }
  }
  const auto globalCell = [&](Index b, Index i, Index j) {
    return cellOffset[b] + (j * spec.blocks[b].nx) + i;
  };

  // --- Faces: per block, vertical then horizontal (the single-block order);
  // an interface side contributes its faces once, from its `first` block.
  std::vector<Face> faces;
  // sideFaceIds[block][side][k] -> face id (boundary sides only).
  std::vector<std::array<std::vector<Index>, 4>> sideFaceIds(spec.blocks.size());
  std::vector<std::pair<Vector2, Vector2>> boundarySegments;  // oriented, domain on the left
  std::vector<BlockSideRef> boundarySegmentSide;
  Index nextFaceId = 0;

  const auto addSideFace = [&](Index b, BlockSide side, Index k, const FaceGeometry& g) {
    const auto [i, j] = sideCell(spec.blocks[b], side, k);
    const Index owner = globalCell(b, i, j);
    const auto& link = partner[b][static_cast<int>(side)];
    if (link.has_value() && !link->second) return;  // created by the `first` side
    const Index faceId = nextFaceId++;
    if (link.has_value()) {
      const auto& iface = spec.interfaces[link->first];
      const BlockSpec& other = spec.blocks[iface.second.block];
      const Index kk = iface.reversed ? sideFaceCount(other, iface.second.side) - 1 - k : k;
      const auto [oi, oj] = sideCell(other, iface.second.side, kk);
      const Index neighbor = globalCell(iface.second.block, oi, oj);
      faces.emplace_back(faceId, owner, neighbor, g.centroid, g.areaVector);
      cells[owner].addFace(faceId);
      cells[neighbor].addFace(faceId);
      return;
    }
    faces.emplace_back(faceId, owner, std::nullopt, g.centroid, g.areaVector);
    cells[owner].addFace(faceId);
    sideFaceIds[b][static_cast<int>(side)].push_back(faceId);
  };

  for (std::size_t b = 0; b < spec.blocks.size(); ++b) {
    const BlockSpec& block = spec.blocks[b];
    const Index stride = block.nx + 1;
    const auto v = [&](Index i, Index j) -> const Vector2& {
      return block.vertices[(j * stride) + i];
    };
    for (Index j = 0; j < block.ny; ++j) {
      for (Index i = 0; i <= block.nx; ++i) {
        const FaceGeometry g = verticalEdgeFace(v(i, j), v(i, j + 1), i == 0);
        if (i == 0) {
          addSideFace(b, BlockSide::Left, j, g);
        } else if (i == block.nx) {
          addSideFace(b, BlockSide::Right, j, g);
        } else {
          const Index faceId = nextFaceId++;
          const Index owner = globalCell(b, i - 1, j);
          const Index neighbor = globalCell(b, i, j);
          faces.emplace_back(faceId, owner, neighbor, g.centroid, g.areaVector);
          cells[owner].addFace(faceId);
          cells[neighbor].addFace(faceId);
        }
      }
    }
    for (Index j = 0; j <= block.ny; ++j) {
      for (Index i = 0; i < block.nx; ++i) {
        const FaceGeometry g = horizontalEdgeFace(v(i, j), v(i + 1, j), j == 0);
        if (j == 0) {
          addSideFace(b, BlockSide::Bottom, i, g);
        } else if (j == block.ny) {
          addSideFace(b, BlockSide::Top, i, g);
        } else {
          const Index faceId = nextFaceId++;
          const Index owner = globalCell(b, i, j - 1);
          const Index neighbor = globalCell(b, i, j);
          faces.emplace_back(faceId, owner, neighbor, g.centroid, g.areaVector);
          cells[owner].addFace(faceId);
          cells[neighbor].addFace(faceId);
        }
      }
    }
    // Boundary segments of this block, oriented counter-clockwise around
    // the block (so the domain lies on their left).
    for (const BlockSide side :
         {BlockSide::Bottom, BlockSide::Right, BlockSide::Top, BlockSide::Left}) {
      if (partner[b][static_cast<int>(side)].has_value()) continue;
      const Index n = sideFaceCount(block, side);
      const bool forward = side == BlockSide::Bottom || side == BlockSide::Right;
      for (Index k = 0; k < n; ++k) {
        const Vector2& p = sideVertex(block, side, k);
        const Vector2& q = sideVertex(block, side, k + 1);
        boundarySegments.emplace_back(forward ? p : q, forward ? q : p);
        boundarySegmentSide.push_back(BlockSideRef{b, side});
      }
    }
  }

  // --- Boundary curve: the boundary faces must form non-intersecting closed
  // curves -- two boundary faces may meet only at a shared end vertex.
  // Coincident faces are an undeclared interface (or a duplicated block);
  // faces overlapping along a line, a vertex touching another face's
  // interior (hanging vertex, non-conformal side) or crossing faces are
  // overlapping or non-conformally touching blocks. Distances within
  // 1e-9 x the domain size count as touching. Candidate pairs by an x sweep.
  {
    Real xmin = boundarySegments.front().first.x, xmax = xmin;
    Real ymin = boundarySegments.front().first.y, ymax = ymin;
    for (const auto& [p, q] : boundarySegments) {
      for (const Vector2& v : {p, q}) {
        xmin = std::min(xmin, v.x);
        xmax = std::max(xmax, v.x);
        ymin = std::min(ymin, v.y);
        ymax = std::max(ymax, v.y);
      }
    }
    const Real tol = 1e-9 * std::max(xmax - xmin, ymax - ymin);
    std::vector<std::size_t> order(boundarySegments.size());
    for (std::size_t s = 0; s < order.size(); ++s) order[s] = s;
    const auto lowX = [&](std::size_t s) {
      return std::min(boundarySegments[s].first.x, boundarySegments[s].second.x);
    };
    std::sort(order.begin(), order.end(),
              [&](std::size_t l, std::size_t r) { return lowX(l) < lowX(r); });
    const auto near = [&](const Vector2& p, const Vector2& q) { return magnitude(p - q) <= tol; };
    const auto pointSegmentDistance = [](const Vector2& p, const Vector2& a, const Vector2& b) {
      const Vector2 ab = b - a;
      const Real t = std::clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
      return magnitude(p - (a + (t * ab)));
    };
    const auto orient = [](const Vector2& a, const Vector2& b, const Vector2& c) {
      return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
    };
    const auto problem = [&](std::size_t s, std::size_t t, const std::string& what,
                             const std::string& advice) {
      fail("boundary faces of " + describeSide(spec, boundarySegmentSide[s]) + " " +
           describePoint(boundarySegments[s].first) + "-" +
           describePoint(boundarySegments[s].second) + " and " +
           describeSide(spec, boundarySegmentSide[t]) + " " +
           describePoint(boundarySegments[t].first) + "-" +
           describePoint(boundarySegments[t].second) + " " + what + " (" + advice + ")");
    };
    for (std::size_t m = 0; m < order.size(); ++m) {
      const std::size_t s = order[m];
      const auto& [a, b] = boundarySegments[s];
      const Real sHighX = std::max(a.x, b.x);
      for (std::size_t n = m + 1; n < order.size() && lowX(order[n]) <= sHighX + tol; ++n) {
        const std::size_t t = order[n];
        const auto& [c, d] = boundarySegments[t];
        if (std::min(c.y, d.y) > std::max(a.y, b.y) + tol ||
            std::max(c.y, d.y) < std::min(a.y, b.y) - tol) {
          continue;
        }
        if ((near(a, c) && near(b, d)) || (near(a, d) && near(b, c))) {
          problem(s, t, "coincide",
                  "blocks that touch along a side must be joined by an interface; duplicated or "
                  "overlapping blocks are not allowed");
        }
        // {shared end, other end of s, other end of t} for faces meeting at a vertex.
        std::optional<std::array<Vector2, 3>> meeting;
        for (const auto& [sEnd, sOther] : {std::pair{a, b}, std::pair{b, a}}) {
          for (const auto& [tEnd, tOther] : {std::pair{c, d}, std::pair{d, c}}) {
            if (near(sEnd, tEnd)) meeting = std::array<Vector2, 3>{sEnd, sOther, tOther};
          }
        }
        if (meeting.has_value()) {
          // Faces meeting at a vertex must not fold back over each other.
          const auto& [shared, sOther, tOther] = *meeting;
          if (dot(sOther - shared, tOther - shared) > 0.0 &&
              (pointSegmentDistance(tOther, shared, sOther) <= tol ||
               pointSegmentDistance(sOther, shared, tOther) <= tol)) {
            problem(s, t, "overlap",
                    "blocks must not overlap; sides that touch must be conformal interfaces");
          }
          continue;
        }
        const Real o1 = orient(a, b, c), o2 = orient(a, b, d);
        const Real o3 = orient(c, d, a), o4 = orient(c, d, b);
        const bool crossing = ((o1 > 0.0 && o2 < 0.0) || (o1 < 0.0 && o2 > 0.0)) &&
                              ((o3 > 0.0 && o4 < 0.0) || (o3 < 0.0 && o4 > 0.0));
        const Real distance =
            crossing ? 0.0
                     : std::min({pointSegmentDistance(a, c, d), pointSegmentDistance(b, c, d),
                                 pointSegmentDistance(c, a, b), pointSegmentDistance(d, a, b)});
        if (distance <= tol) {
          problem(s, t, "cross or touch",
                  "block boundaries may meet only at shared vertices; overlapping blocks, "
                  "hanging vertices and non-conformal sides are not allowed");
        }
      }
    }
  }

  // --- Overlap: every cell centroid must lie inside the domain exactly once
  // (winding number 1 with respect to the oriented boundary: outer loops
  // counter-clockwise, holes clockwise). Overlapping, duplicated or
  // self-overlapping blocks give 2 or more; a centroid outside the
  // boundary (inverted topology) gives 0.
  {
    std::size_t reported = 0;
    std::string problems;
    for (std::size_t b = 0; b < spec.blocks.size() && reported < 5; ++b) {
      const BlockSpec& block = spec.blocks[b];
      for (Index j = 0; j < block.ny && reported < 5; ++j) {
        for (Index i = 0; i < block.nx && reported < 5; ++i) {
          const Cell& cell = cells[globalCell(b, i, j)];
          const int w = windingNumber(cell.centroid(), boundarySegments);
          if (w != 1) {
            problems += "; block '" + block.name + "' cell (" + std::to_string(i) + "," +
                        std::to_string(j) + ") at " + describePoint(cell.centroid()) +
                        " is covered " + std::to_string(w) + " times";
            ++reported;
          }
        }
      }
    }
    if (reported > 0) {
      fail(
          "blocks overlap or fold over each other (every cell must lie inside the domain "
          "exactly once)" +
          problems);
    }
  }

  // --- Patches, grids ------------------------------------------------------------
  std::vector<BoundaryPatch> patches;
  patches.reserve(spec.patches.size());
  for (const auto& patch : spec.patches) {
    std::vector<Index> ids;
    for (const auto& ref : patch.sides) {
      const auto& sideIds = sideFaceIds[ref.block][static_cast<int>(ref.side)];
      ids.insert(ids.end(), sideIds.begin(), sideIds.end());
    }
    patches.emplace_back(patch.name, std::move(ids));
  }
  std::vector<StructuredGrid> grids;
  grids.reserve(spec.blocks.size());
  for (const auto& block : spec.blocks) {
    grids.push_back(StructuredGrid{block.nx, block.ny, block.vertices, block.name});
  }
  return Mesh(std::move(cells), std::move(faces), std::move(patches), std::move(grids));
}

// --- P12-MESH-005: 3D Cartesian hexahedral mesh ---------------------------------

Mesh MeshGeometry::createCartesian3D(Index nx, Index ny, Index nz, Real lengthX, Real lengthY,
                                     Real lengthZ, const Vector3& origin) {
  validateCartesianInputs(nx, ny, lengthX, lengthY);
  if (nz == 0) {
    throw InvalidArgumentError("Cartesian mesh requires nz > 0");
  }
  if (!std::isfinite(lengthZ) || !(lengthZ > 0.0)) {
    throw InvalidArgumentError("Cartesian mesh requires a finite, positive lengthZ");
  }
  if (!isFinite(origin)) {
    throw InvalidArgumentError("Cartesian mesh requires a finite origin");
  }

  const AxisSpacing x = AxisSpacing::uniform(nx, lengthX);
  const AxisSpacing y = AxisSpacing::uniform(ny, lengthY);
  const AxisSpacing z = AxisSpacing::uniform(nz, lengthZ);
  const Real dx = x.width(0);
  const Real dy = y.width(0);
  const Real dz = z.width(0);
  const Real volume = (dx * dy) * dz;
  const Real areaX = dy * dz;  // x-face (normal +-x)
  const Real areaY = dx * dz;
  const Real areaZ = dx * dy;
  const auto cellIndex = [nx, ny](Index i, Index j, Index k) noexcept -> Index {
    return (((k * ny) + j) * nx) + i;
  };

  std::vector<Cell> cells;
  cells.reserve(nx * ny * nz);
  for (Index k = 0; k < nz; ++k) {
    for (Index j = 0; j < ny; ++j) {
      for (Index i = 0; i < nx; ++i) {
        cells.emplace_back(
            cellIndex(i, j, k),
            Vector3{origin.x + x.center(i), origin.y + y.center(j), origin.z + z.center(k)},
            volume);
      }
    }
  }

  std::vector<Face> faces;
  faces.reserve(((nx + 1) * ny * nz) + (nx * (ny + 1) * nz) + (nx * ny * (nz + 1)));
  std::vector<std::vector<Index>> patchFaces(6);
  Index nextFaceId = 0;
  // One face between cell `low` (on the negative side) and cell `high`, or a
  // boundary face of the one existing cell: `atLow` / `atHigh` say which
  // side of the domain the face lies on (a boundary face's area vector is
  // the outward one, i.e. -normal on the low side); `lowPatch` / `lowPatch
  // + 1` are the patches of the two sides.
  const auto addFace = [&](const Vector3& centroid, const Vector3& positiveAreaVector, bool atLow,
                           bool atHigh, Index low, Index high, std::size_t lowPatch) {
    const Index faceId = nextFaceId++;
    if (atLow) {
      faces.emplace_back(faceId, high, std::nullopt, centroid, -positiveAreaVector);
      patchFaces[lowPatch].push_back(faceId);
      cells[high].addFace(faceId);
    } else if (atHigh) {
      faces.emplace_back(faceId, low, std::nullopt, centroid, positiveAreaVector);
      patchFaces[lowPatch + 1].push_back(faceId);
      cells[low].addFace(faceId);
    } else {
      faces.emplace_back(faceId, low, high, centroid, positiveAreaVector);
      cells[low].addFace(faceId);
      cells[high].addFace(faceId);
    }
  };

  for (Index k = 0; k < nz; ++k) {
    for (Index j = 0; j < ny; ++j) {
      for (Index i = 0; i <= nx; ++i) {
        addFace(Vector3{origin.x + x.node(i), origin.y + y.center(j), origin.z + z.center(k)},
                Vector3{areaX, 0.0, 0.0}, i == 0, i == nx, i == 0 ? 0 : cellIndex(i - 1, j, k),
                i == nx ? 0 : cellIndex(i, j, k), 0);
      }
    }
  }
  for (Index k = 0; k < nz; ++k) {
    for (Index j = 0; j <= ny; ++j) {
      for (Index i = 0; i < nx; ++i) {
        addFace(Vector3{origin.x + x.center(i), origin.y + y.node(j), origin.z + z.center(k)},
                Vector3{0.0, areaY, 0.0}, j == 0, j == ny, j == 0 ? 0 : cellIndex(i, j - 1, k),
                j == ny ? 0 : cellIndex(i, j, k), 2);
      }
    }
  }
  for (Index k = 0; k <= nz; ++k) {
    for (Index j = 0; j < ny; ++j) {
      for (Index i = 0; i < nx; ++i) {
        addFace(Vector3{origin.x + x.center(i), origin.y + y.center(j), origin.z + z.node(k)},
                Vector3{0.0, 0.0, areaZ}, k == 0, k == nz, k == 0 ? 0 : cellIndex(i, j, k - 1),
                k == nz ? 0 : cellIndex(i, j, k), 4);
      }
    }
  }

  static const char* const kPatchNames[6] = {"xmin", "xmax", "ymin", "ymax", "zmin", "zmax"};
  std::vector<BoundaryPatch> patches;
  patches.reserve(6);
  for (std::size_t p = 0; p < 6; ++p)
    patches.emplace_back(kPatchNames[p], std::move(patchFaces[p]));

  // The generating vertex grid (export only; the numerics never read it).
  StructuredGrid grid{nx, ny, {}, {}, nz};
  grid.vertices.reserve((nx + 1) * (ny + 1) * (nz + 1));
  for (Index k = 0; k <= nz; ++k) {
    for (Index j = 0; j <= ny; ++j) {
      for (Index i = 0; i <= nx; ++i) {
        grid.vertices.push_back(
            Vector3{origin.x + x.node(i), origin.y + y.node(j), origin.z + z.node(k)});
      }
    }
  }
  return Mesh(std::move(cells), std::move(faces), std::move(patches), std::move(grid));
}

// --- P12-MESH-007: topology-preserving geometry update -----------------------------
// (results/p12-mesh-007/architecture.md section 3.3.)

namespace {

// Corner number of the hexahedron corner (a, b, c) in {0, 1}^3, in the
// StructuredTopology::CellCorners order: (0,0,0), (1,0,0), (1,1,0), (0,1,0),
// then the same four at c = 1.
constexpr int hexCorner(int a, int b, int c) noexcept {
  return (c != 0 ? 4 : 0) + (b != 0 ? (a != 0 ? 2 : 3) : (a != 0 ? 1 : 0));
}

// The six faces of a hexahedron as corner quadruples p0..p3 with
// 1/2 (p2 - p0) x (p3 - p1) pointing OUT of the cell: west (-x), east (+x),
// south (-y), north (+y), bottom (-z), top (+z).
constexpr int kHexFaces[6][4] = {{0, 4, 7, 3}, {1, 2, 6, 5}, {0, 1, 5, 4},
                                 {3, 7, 6, 2}, {0, 3, 2, 1}, {4, 5, 6, 7}};

// Two-point Gauss-Legendre abscissae on [0, 1] (weights 1/2 each).
const Real kGaussHalfOffset = 0.5 / std::sqrt(3.0);
const Real kGauss2[2] = {0.5 - kGaussHalfOffset, 0.5 + kGaussHalfOffset};

// d x / d xi, d eta, d zeta of the trilinear map of the corners x[0..7] at
// (xi, eta, zeta) in [0, 1]^3.
void trilinearDerivatives(const Vector3 (&x)[8], Real xi, Real eta, Real zeta, Vector3& dxi,
                          Vector3& deta, Vector3& dzeta) noexcept {
  dxi = ((x[1] - x[0]) * ((1.0 - eta) * (1.0 - zeta))) + ((x[2] - x[3]) * (eta * (1.0 - zeta))) +
        ((x[5] - x[4]) * ((1.0 - eta) * zeta)) + ((x[6] - x[7]) * (eta * zeta));
  deta = ((x[3] - x[0]) * ((1.0 - xi) * (1.0 - zeta))) + ((x[2] - x[1]) * (xi * (1.0 - zeta))) +
         ((x[7] - x[4]) * ((1.0 - xi) * zeta)) + ((x[6] - x[5]) * (xi * zeta));
  dzeta = ((x[4] - x[0]) * ((1.0 - xi) * (1.0 - eta))) + ((x[5] - x[1]) * (xi * (1.0 - eta))) +
          ((x[6] - x[2]) * (xi * eta)) + ((x[7] - x[3]) * ((1.0 - xi) * eta));
}

Vector3 trilinearPoint(const Vector3 (&x)[8], Real xi, Real eta, Real zeta) noexcept {
  const Real a = 1.0 - xi;
  const Real b = 1.0 - eta;
  const Real c = 1.0 - zeta;
  return (x[0] * (a * b * c)) + (x[1] * (xi * b * c)) + (x[2] * (xi * eta * c)) +
         (x[3] * (a * eta * c)) + (x[4] * (a * b * zeta)) + (x[5] * (xi * b * zeta)) +
         (x[6] * (xi * eta * zeta)) + (x[7] * (a * eta * zeta));
}

// A valid hexahedron has a positive Jacobian determinant at all eight
// corners (each corner's three edges form a right-handed frame). Names the
// first corner that fails, or nullopt.
std::optional<std::string> hexDefect(const Vector3 (&x)[8]) {
  for (int c = 0; c <= 1; ++c) {
    for (int b = 0; b <= 1; ++b) {
      for (int a = 0; a <= 1; ++a) {
        const Vector3 dxi = x[hexCorner(1, b, c)] - x[hexCorner(0, b, c)];
        const Vector3 deta = x[hexCorner(a, 1, c)] - x[hexCorner(a, 0, c)];
        const Vector3 dzeta = x[hexCorner(a, b, 1)] - x[hexCorner(a, b, 0)];
        const Real det = dot(dxi, cross(deta, dzeta));
        if (!(det > 0.0)) {
          std::ostringstream out;
          out.precision(6);
          out << "the corner Jacobian determinant at corner (" << a << "," << b << "," << c
              << ") is " << det << " <= 0 (inverted or degenerate hexahedron)";
          return out.str();
        }
      }
    }
  }
  return std::nullopt;
}

// Trilinear hexahedron: volume = integral of det J, centroid = integral of
// x det J / volume, by 2 x 2 x 2 Gauss (exact: det J has degree <= 2 and x
// degree 1 in each variable).
CellGeometry hexGeometry(const Vector3 (&x)[8]) noexcept {
  Real volume = 0.0;
  Vector3 moment{};
  for (const Real xi : kGauss2) {
    for (const Real eta : kGauss2) {
      for (const Real zeta : kGauss2) {
        Vector3 dxi;
        Vector3 deta;
        Vector3 dzeta;
        trilinearDerivatives(x, xi, eta, zeta, dxi, deta, dzeta);
        const Real weight = 0.125 * dot(dxi, cross(deta, dzeta));
        volume += weight;
        moment += trilinearPoint(x, xi, eta, zeta) * weight;
      }
    }
  }
  return CellGeometry{moment * (1.0 / volume), volume};
}

// Bilinear face p0..p3: area vector 1/2 (p2 - p0) x (p3 - p1) (the exact
// vector area of the surface); centroid = the projected-area-weighted
// centroid of the four triangles (p_t, p_t+1, m), m = the vertex average
// (the exact area centroid when the face is planar).
FaceGeometry bilinearFace(const Vector3 (&p)[4]) noexcept {
  const Vector3 areaVector = cross(p[2] - p[0], p[3] - p[1]) * 0.5;
  const Vector3 mid = ((p[0] + p[1]) + (p[2] + p[3])) * 0.25;
  const Vector3 normal = areaVector * (1.0 / magnitude(areaVector));
  Real weightSum = 0.0;
  Vector3 weighted{};
  for (int t = 0; t < 4; ++t) {
    const Vector3& a = p[t];
    const Vector3& b = p[(t + 1) % 4];
    const Real weight = dot(cross(b - a, mid - a) * 0.5, normal);
    weightSum += weight;
    weighted += ((a + b) + mid) * (weight / 3.0);
  }
  return FaceGeometry{weighted * (1.0 / weightSum), areaVector};
}

// Signed volume swept by the bilinear face p (corners moving linearly by d
// during the step): integral over s, r, t in [0, 1] of xdot . (x_s x x_r),
// 2 x 2 x 2 Gauss (degree <= 2 per variable). Positive along 1/2 (p2 - p0)
// x (p3 - p1).
Real sweptBilinear(const Vector3 (&p)[4], const Vector3 (&d)[4]) noexcept {
  Real total = 0.0;
  for (const Real t : kGauss2) {
    const Vector3 q[4] = {p[0] + (d[0] * t), p[1] + (d[1] * t), p[2] + (d[2] * t),
                          p[3] + (d[3] * t)};
    for (const Real s : kGauss2) {
      for (const Real r : kGauss2) {
        const Vector3 xs = ((q[1] - q[0]) * (1.0 - r)) + ((q[2] - q[3]) * r);
        const Vector3 xr = ((q[3] - q[0]) * (1.0 - s)) + ((q[2] - q[1]) * s);
        const Vector3 xd = (d[0] * ((1.0 - s) * (1.0 - r))) + (d[1] * (s * (1.0 - r))) +
                           (d[2] * (s * r)) + (d[3] * ((1.0 - s) * r));
        total += dot(xd, cross(xs, xr));
      }
    }
  }
  return total * 0.125;
}

std::string cellLocation(const MeshGeometry::StructuredTopology& topology, Index cellId) {
  const auto& cell = topology.cells[cellId];
  std::string where;
  if (topology.blockNames.size() > 1 || !topology.blockNames[cell.block].empty()) {
    where = "block '" + topology.blockNames[cell.block] + "' ";
  }
  where += "cell (" + std::to_string(cell.i) + "," + std::to_string(cell.j);
  if (topology.dimension == 3) where += "," + std::to_string(cell.k);
  return where + ")";
}

void requireVertexCount(const MeshGeometry::StructuredTopology& topology,
                        const std::vector<Vector3>& vertices, const char* function) {
  if (vertices.size() != topology.vertices.size()) {
    throw InvalidArgumentError(std::string(function) + ": expected " +
                               std::to_string(topology.vertices.size()) + " vertices, got " +
                               std::to_string(vertices.size()));
  }
}

}  // namespace

MeshGeometry::StructuredTopology MeshGeometry::structuredTopology(const Mesh& mesh) {
  const auto& blocks = mesh.structuredBlocks();
  if (blocks.empty()) {
    throw InvalidArgumentError(
        "structuredTopology: the mesh has no structured vertex grid (only a mesh built from "
        "structured grids can move)");
  }
  StructuredTopology topology;
  topology.dimension = mesh.dimension();

  // --- Welded vertices: equal coordinates are one vertex (the multi-block
  // builder requires interface vertices to be exactly equal).
  std::map<std::tuple<Real, Real, Real>, Index> welded;
  topology.blockVertexIds.resize(blocks.size());
  for (std::size_t b = 0; b < blocks.size(); ++b) {
    topology.blockNames.push_back(blocks[b].name);
    topology.blockVertexIds[b].reserve(blocks[b].vertices.size());
    for (const Vector3& p : blocks[b].vertices) {
      const auto [it, inserted] =
          welded.emplace(std::make_tuple(p.x, p.y, p.z), topology.vertices.size());
      if (inserted) topology.vertices.push_back(p);
      topology.blockVertexIds[b].push_back(it->second);
    }
  }

  // --- Cells, block by block in cell-id order (the Mesh invariant).
  topology.cells.resize(mesh.numberOfCells());
  Index offset = 0;
  for (std::size_t b = 0; b < blocks.size(); ++b) {
    const StructuredGrid& g = blocks[b];
    const auto& ids = topology.blockVertexIds[b];
    const auto vertex = [&](Index i, Index j, Index k) {
      return ids[g.isThreeDimensional() ? (((k * (g.ny + 1)) + j) * (g.nx + 1)) + i
                                        : (j * (g.nx + 1)) + i];
    };
    const Index nz = g.isThreeDimensional() ? g.nz : 1;
    for (Index k = 0; k < nz; ++k) {
      for (Index j = 0; j < g.ny; ++j) {
        for (Index i = 0; i < g.nx; ++i) {
          StructuredTopology::CellCorners corners;
          corners.block = b;
          corners.i = i;
          corners.j = j;
          corners.k = k;
          corners.corner[0] = vertex(i, j, k);
          corners.corner[1] = vertex(i + 1, j, k);
          corners.corner[2] = vertex(i + 1, j + 1, k);
          corners.corner[3] = vertex(i, j + 1, k);
          if (g.isThreeDimensional()) {
            corners.corner[4] = vertex(i, j, k + 1);
            corners.corner[5] = vertex(i + 1, j, k + 1);
            corners.corner[6] = vertex(i + 1, j + 1, k + 1);
            corners.corner[7] = vertex(i, j + 1, k + 1);
          }
          topology.cells[offset + (((k * g.ny) + j) * g.nx) + i] = corners;
        }
      }
    }
    offset += g.cellCount();
  }

  // --- Faces: each face is the side of its owner cell whose midpoint (2D) /
  // vertex average (3D) is nearest to the face centroid. The stored area
  // vector always points out of the owner, so the owner's outward side fixes
  // the orientation: 2D left/bottom sides use the builder's negated formula.
  const auto& vertices = topology.vertices;
  topology.faces.resize(mesh.numberOfFaces());
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const Face& face = mesh.face(f);
    const auto& owner = topology.cells[face.owner()];
    Real best = std::numeric_limits<Real>::infinity();
    StructuredTopology::FaceVertices chosen;
    if (topology.dimension == 2) {
      const auto& c = owner.corner;  // (i,j), (i+1,j), (i+1,j+1), (i,j+1)
      const StructuredTopology::FaceVertices sides[4] = {
          {{c[0], c[3], 0, 0}, true, true},     // left:   (i,j) -> (i,j+1), outward -i
          {{c[1], c[2], 0, 0}, true, false},    // right:  (i+1,j) -> (i+1,j+1), +i
          {{c[0], c[1], 0, 0}, false, true},    // bottom: (i,j) -> (i+1,j), -j
          {{c[3], c[2], 0, 0}, false, false}};  // top:    (i,j+1) -> (i+1,j+1), +j
      for (const auto& side : sides) {
        const Real distance = magnitude(
            ((vertices[side.vertex[0]] + vertices[side.vertex[1]]) * 0.5) - face.centroid());
        if (distance < best) {
          best = distance;
          chosen = side;
        }
      }
    } else {
      for (const auto& corners : kHexFaces) {
        StructuredTopology::FaceVertices side{{owner.corner[corners[0]], owner.corner[corners[1]],
                                               owner.corner[corners[2]], owner.corner[corners[3]]},
                                              false,
                                              false};
        const Vector3 average = ((vertices[side.vertex[0]] + vertices[side.vertex[1]]) +
                                 (vertices[side.vertex[2]] + vertices[side.vertex[3]])) *
                                0.25;
        const Real distance = magnitude(average - face.centroid());
        if (distance < best) {
          best = distance;
          chosen = side;
        }
      }
    }
    topology.faces[f] = chosen;
  }

  // --- Verification: the map must reproduce the mesh's own geometry.
  const MeshGeometryState recomputed = computeGeometry(topology, topology.vertices);
  const Real dimensionPower = 1.0 / static_cast<Real>(topology.dimension);
  for (Index c = 0; c < mesh.numberOfCells(); ++c) {
    const Cell& cell = mesh.cell(c);
    const Real size = std::pow(cell.volume(), dimensionPower);
    if (std::abs(recomputed.cellVolumes[c] - cell.volume()) > 1e-9 * cell.volume() ||
        magnitude(recomputed.cellCentroids[c] - cell.centroid()) > 1e-9 * size) {
      throw InvalidArgumentError("structuredTopology: " + cellLocation(topology, c) + " (cell " +
                                 std::to_string(c) +
                                 ") does not have the geometry of the mesh's structured grid");
    }
  }
  for (Index f = 0; f < mesh.numberOfFaces(); ++f) {
    const Face& face = mesh.face(f);
    const Real size = topology.dimension == 3 ? std::sqrt(face.area()) : face.area();
    if (magnitude(recomputed.faceAreaVectors[f] - face.areaVector()) > 1e-9 * face.area() ||
        magnitude(recomputed.faceCentroids[f] - face.centroid()) > 1e-9 * size) {
      throw InvalidArgumentError("structuredTopology: face " + std::to_string(f) +
                                 " does not have the geometry of the mesh's structured grid");
    }
  }
  return topology;
}

MeshGeometryState MeshGeometry::computeGeometry(const StructuredTopology& topology,
                                                const std::vector<Vector3>& vertices) {
  requireVertexCount(topology, vertices, "computeGeometry");
  for (std::size_t v = 0; v < vertices.size(); ++v) {
    if (!isFinite(vertices[v])) {
      throw InvalidArgumentError("computeGeometry: vertex " + std::to_string(v) +
                                 " has a non-finite coordinate");
    }
  }
  MeshGeometryState geometry;
  const std::size_t cellCount = topology.cells.size();
  const std::size_t faceCount = topology.faces.size();
  geometry.cellCentroids.resize(cellCount);
  geometry.cellVolumes.resize(cellCount);
  geometry.faceCentroids.resize(faceCount);
  geometry.faceAreaVectors.resize(faceCount);

  for (std::size_t c = 0; c < cellCount; ++c) {
    const auto& corner = topology.cells[c].corner;
    if (topology.dimension == 2) {
      const Vector2 corners[4] = {vertices[corner[0]], vertices[corner[1]], vertices[corner[2]],
                                  vertices[corner[3]]};
      const std::optional<CellGeometry> g = quadCellGeometry(corners);
      if (!g.has_value()) {
        throw InvalidArgumentError("mesh motion: " + cellLocation(topology, c) +
                                   " is not a strictly convex counter-clockwise quadrilateral: " +
                                   quadCellDefect(corners));
      }
      geometry.cellCentroids[c] = g->centroid;
      geometry.cellVolumes[c] = g->volume;
    } else {
      const Vector3 x[8] = {vertices[corner[0]], vertices[corner[1]], vertices[corner[2]],
                            vertices[corner[3]], vertices[corner[4]], vertices[corner[5]],
                            vertices[corner[6]], vertices[corner[7]]};
      if (const std::optional<std::string> defect = hexDefect(x); defect.has_value()) {
        throw InvalidArgumentError("mesh motion: " + cellLocation(topology, c) +
                                   " is not a valid hexahedron: " + *defect);
      }
      const CellGeometry g = hexGeometry(x);
      geometry.cellCentroids[c] = g.centroid;
      geometry.cellVolumes[c] = g.volume;
    }
  }

  for (std::size_t f = 0; f < faceCount; ++f) {
    const auto& face = topology.faces[f];
    FaceGeometry g;
    if (topology.dimension == 2) {
      g = face.vertical
              ? verticalEdgeFace(vertices[face.vertex[0]], vertices[face.vertex[1]], face.negate)
              : horizontalEdgeFace(vertices[face.vertex[0]], vertices[face.vertex[1]], face.negate);
    } else {
      const Vector3 p[4] = {vertices[face.vertex[0]], vertices[face.vertex[1]],
                            vertices[face.vertex[2]], vertices[face.vertex[3]]};
      g = bilinearFace(p);
    }
    geometry.faceCentroids[f] = g.centroid;
    geometry.faceAreaVectors[f] = g.areaVector;
  }

  geometry.blockVertices.resize(topology.blockVertexIds.size());
  for (std::size_t b = 0; b < topology.blockVertexIds.size(); ++b) {
    geometry.blockVertices[b].reserve(topology.blockVertexIds[b].size());
    for (const Index v : topology.blockVertexIds[b])
      geometry.blockVertices[b].push_back(vertices[v]);
  }
  return geometry;
}

std::vector<Real> MeshGeometry::sweptVolumes(const StructuredTopology& topology,
                                             const std::vector<Vector3>& from,
                                             const std::vector<Vector3>& to) {
  requireVertexCount(topology, from, "sweptVolumes");
  requireVertexCount(topology, to, "sweptVolumes");
  std::vector<Real> swept(topology.faces.size(), 0.0);
  for (std::size_t f = 0; f < topology.faces.size(); ++f) {
    const auto& face = topology.faces[f];
    if (topology.dimension == 2) {
      const Vector3& a0 = from[face.vertex[0]];
      const Vector3& b0 = from[face.vertex[1]];
      const Vector3& a1 = to[face.vertex[0]];
      const Vector3& b1 = to[face.vertex[1]];
      const Vector3 aMid = (a0 + a1) * 0.5;
      const Vector3 bMid = (b0 + b1) * 0.5;
      const Vector3 areaMid = face.vertical
                                  ? verticalEdgeFace(aMid, bMid, face.negate).areaVector
                                  : horizontalEdgeFace(aMid, bMid, face.negate).areaVector;
      swept[f] = dot(areaMid, ((a1 - a0) + (b1 - b0)) * 0.5);
    } else {
      const Vector3 p[4] = {from[face.vertex[0]], from[face.vertex[1]], from[face.vertex[2]],
                            from[face.vertex[3]]};
      const Vector3 d[4] = {to[face.vertex[0]] - p[0], to[face.vertex[1]] - p[1],
                            to[face.vertex[2]] - p[2], to[face.vertex[3]] - p[3]};
      swept[f] = sweptBilinear(p, d);
    }
  }
  return swept;
}

}  // namespace cfd::mesh

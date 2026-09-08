#include "cfd/mesh/MeshGeometry.hpp"

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

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
                                                        const Face& boundaryFace) {
  const Vector2 outwardBoundaryNormal = unitNormal(boundaryFace);

  std::optional<Index> best;
  Real bestDot = 0.0;
  for (const Index faceId : cell.faceIds()) {
    if (faceId == boundaryFace.id()) {
      continue;
    }
    const Face& candidate = mesh.face(faceId);
    if (candidate.isBoundary()) {
      continue;
    }
    const Vector2 rawNormal = unitNormal(candidate);
    const Vector2 outwardFromCell =
        (candidate.owner() == cell.id()) ? rawNormal : (rawNormal * -1.0);
    const Real alignment = dot(outwardBoundaryNormal, outwardFromCell);
    if (!best.has_value() || alignment < bestDot) {
      bestDot = alignment;
      best = faceId;
    }
  }
  return best;
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

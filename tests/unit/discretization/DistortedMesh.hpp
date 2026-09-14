#pragma once

// P12-NUM-002: the minimum deterministic distorted-quadrilateral mesh
// capability needed to verify gradient reconstruction on a non-Cartesian
// mesh (P12-NUM-002 requirement 6 / TODO.md's own note that "distorted-
// mesh tests are not currently possible" was the blocker closing this
// item). Deliberately narrow: a single free function building one
// structured-topology but geometrically-distorted 2D quad mesh, using
// only existing, already-verified Mesh/Cell/Face/BoundaryPatch
// constructors -- not a general unstructured-mesh importer/remesher/
// framework (explicitly out of scope, see TODO.md P12-NUM-002).
//
// cfd::mesh::Cell/Face store only derived geometry (centroid, volume,
// area vector) -- there is no underlying vertex list in this codebase's
// mesh data model. So "distorting the mesh" here means: define an
// explicit (nx+1)x(ny+1) logical vertex grid, perturb it deterministically
// (smoothly, vanishing at the domain boundary so the OVERALL rectangular
// domain stays exactly controlled), then compute each cell's true
// (possibly non-rectangular) polygon centroid/volume and each face's true
// centroid/area-vector directly from those vertices -- ordinary 2D
// polygon geometry, applied once here.

#include <cmath>
#include <optional>
#include <vector>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::test {

namespace detail {

// Smoothly perturbs a Cartesian vertex position, vanishing exactly on
// all four domain edges (any factor of sin(pi*x/Lx) or sin(pi*y/Ly) is
// zero there) -- so the outer domain boundary stays exactly the
// undistorted rectangle [0,Lx]x[0,Ly] regardless of `amplitude`, only
// INTERIOR vertices actually move. `amplitude` is a length (typically a
// fraction of the smaller cell spacing) -- see `distortionAmplitude`
// callers for the "mild"/"stronger" conventions used in tests.
inline Vector2 perturbVertex(Real x, Real y, Real lengthX, Real lengthY, Real amplitude) {
  const Real sx = std::sin(constants::pi * x / lengthX);
  const Real sy = std::sin(constants::pi * y / lengthY);
  // Independent, differently-shaped x/y perturbations (different x
  // frequency) so the distortion is genuinely 2D shear+skew, not a pure
  // diagonal stretch -- both factors still vanish on every edge (each
  // retains the sy, resp. combined, zero at x/y in {0,Lx}/{0,Ly}).
  const Real dx = amplitude * sx * sy;
  const Real dy = amplitude * std::sin(2.0 * constants::pi * x / lengthX) * sy;
  return Vector2{x + dx, y + dy};
}

}  // namespace detail

// Builds a structured-topology (same cell(i,j)=j*nx+i indexing, same
// "left"/"right"/"bottom"/"top" patch grouping, same face-count/adjacency
// as cfd::mesh::MeshGeometry::createCartesian2D) but geometrically
// distorted 2D quadrilateral mesh over [0,lengthX] x [0, lengthY].
//
// `distortionAmplitude` is an absolute length (not a fraction) -- pass
// e.g. 0.15*min(dx,dy) for a mild distortion or 0.35*min(dx,dy) for a
// stronger one (see test callers). Values large enough to fold a cell
// over on itself (self-intersecting/non-convex/negative-area) throw
// InvalidArgumentError -- verified by checking every resulting cell's
// polygon area is positive, never silently producing a degenerate mesh.
//
// Vertex perturbation: see detail::perturbVertex -- deterministic,
// reproducible (no RNG), vanishes on the outer boundary so the domain
// itself stays the exact controlled rectangle.
// Cell geometry: standard 2D polygon area/centroid formulas (shoelace)
// applied to each cell's 4 (distorted) corner vertices, CCW-ordered.
// Face geometry: each face is the edge between two adjacent vertices;
// its centroid is the edge midpoint and its area vector is the edge
// vector rotated 90 degrees, oriented (matching
// MeshGeometry::createCartesian2D's own documented convention) from
// owner toward neighbor for internal faces and outward from the domain
// for boundary faces.
inline cfd::mesh::Mesh createDistortedQuad2D(Index nx, Index ny, Real lengthX, Real lengthY,
                                             Real distortionAmplitude) {
  if (nx == 0 || ny == 0) {
    throw InvalidArgumentError("createDistortedQuad2D: nx and ny must be > 0");
  }
  if (!(lengthX > 0.0) || !(lengthY > 0.0)) {
    throw InvalidArgumentError("createDistortedQuad2D: lengthX and lengthY must be > 0");
  }
  if (!(distortionAmplitude >= 0.0)) {
    throw InvalidArgumentError("createDistortedQuad2D: distortionAmplitude must be >= 0");
  }

  const Real dx = lengthX / static_cast<Real>(nx);
  const Real dy = lengthY / static_cast<Real>(ny);

  // --- Vertex grid: (nx+1) x (ny+1), vertex(i,j) at logical (i,j). ------
  const auto vertexIndex = [nx](Index i, Index j) noexcept -> Index { return (j * (nx + 1)) + i; };
  std::vector<Vector2> vertices((nx + 1) * (ny + 1));
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Real x = static_cast<Real>(i) * dx;
      const Real y = static_cast<Real>(j) * dy;
      vertices[vertexIndex(i, j)] =
          detail::perturbVertex(x, y, lengthX, lengthY, distortionAmplitude);
    }
  }

  const auto cellIndex = [nx](Index i, Index j) noexcept -> Index { return (j * nx) + i; };

  // --- Cells: polygon centroid/area (shoelace) from the 4 CCW corners. -
  std::vector<cfd::mesh::Cell> cells;
  cells.reserve(nx * ny);
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Vector2& v00 = vertices[vertexIndex(i, j)];
      const Vector2& v10 = vertices[vertexIndex(i + 1, j)];
      const Vector2& v11 = vertices[vertexIndex(i + 1, j + 1)];
      const Vector2& v01 = vertices[vertexIndex(i, j + 1)];
      const Vector2 corners[4] = {v00, v10, v11, v01};

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
      if (!(signedArea > 0.0)) {
        throw InvalidArgumentError(
            "createDistortedQuad2D: distortionAmplitude too large -- cell (" + std::to_string(i) +
            "," + std::to_string(j) + ") has non-positive area (self-intersecting/folded quad)");
      }
      cx /= (6.0 * signedArea);
      cy /= (6.0 * signedArea);

      cells.emplace_back(cellIndex(i, j), Vector2{cx, cy}, signedArea);
    }
  }

  std::vector<cfd::mesh::Face> faces;
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

  // --- Vertical faces (edge from vertex(i,j) to vertex(i,j+1)), column i
  // in [0, nx] -- same owner/neighbor/boundary topology as
  // MeshGeometry::createCartesian2D. Area vector = edge rotated so that
  // (on the undistorted grid) it reduces exactly to that function's own
  // {+dy, 0} internal-face convention; negated for the left boundary to
  // point outward, matching that same function's {-dy, 0}.
  for (Index j = 0; j < ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const Vector2& a = vertices[vertexIndex(i, j)];
      const Vector2& b = vertices[vertexIndex(i, j + 1)];
      const Vector2 centroid = (a + b) * 0.5;
      const Vector2 edge = b - a;
      Vector2 areaVector{edge.y, -edge.x};  // points toward increasing i.
      const Index faceId = nextFaceId++;

      if (i == 0) {
        areaVector = areaVector * -1.0;  // outward from the domain (toward decreasing i).
        const Index owner = cellIndex(0, j);
        faces.emplace_back(faceId, owner, std::nullopt, centroid, areaVector);
        leftFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else if (i == nx) {
        const Index owner = cellIndex(nx - 1, j);
        faces.emplace_back(faceId, owner, std::nullopt, centroid, areaVector);
        rightFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else {
        const Index owner = cellIndex(i - 1, j);
        const Index neighbor = cellIndex(i, j);
        faces.emplace_back(faceId, owner, neighbor, centroid, areaVector);
        cells[owner].addFace(faceId);
        cells[neighbor].addFace(faceId);
      }
    }
  }

  // --- Horizontal faces (edge from vertex(i,j) to vertex(i+1,j)), row j
  // in [0, ny]. Rotated the opposite way so the undistorted grid reduces
  // exactly to createCartesian2D's {0, +dx}/{0, -dx}.
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i < nx; ++i) {
      const Vector2& a = vertices[vertexIndex(i, j)];
      const Vector2& b = vertices[vertexIndex(i + 1, j)];
      const Vector2 centroid = (a + b) * 0.5;
      const Vector2 edge = b - a;
      Vector2 areaVector{-edge.y, edge.x};  // points toward increasing j.
      const Index faceId = nextFaceId++;

      if (j == 0) {
        areaVector = areaVector * -1.0;  // outward from the domain (toward decreasing j).
        const Index owner = cellIndex(i, 0);
        faces.emplace_back(faceId, owner, std::nullopt, centroid, areaVector);
        bottomFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else if (j == ny) {
        const Index owner = cellIndex(i, ny - 1);
        faces.emplace_back(faceId, owner, std::nullopt, centroid, areaVector);
        topFaceIds.push_back(faceId);
        cells[owner].addFace(faceId);
      } else {
        const Index owner = cellIndex(i, j - 1);
        const Index neighbor = cellIndex(i, j);
        faces.emplace_back(faceId, owner, neighbor, centroid, areaVector);
        cells[owner].addFace(faceId);
        cells[neighbor].addFace(faceId);
      }
    }
  }

  std::vector<cfd::mesh::BoundaryPatch> patches;
  patches.reserve(4);
  patches.emplace_back("left", std::move(leftFaceIds));
  patches.emplace_back("right", std::move(rightFaceIds));
  patches.emplace_back("bottom", std::move(bottomFaceIds));
  patches.emplace_back("top", std::move(topFaceIds));

  return cfd::mesh::Mesh(std::move(cells), std::move(faces), std::move(patches));
}

}  // namespace cfd::test

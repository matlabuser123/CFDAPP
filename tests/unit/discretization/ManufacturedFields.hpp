#pragma once

// Test-only helpers for discretization operator verification: analytical
// (manufactured) fields with known exact gradient/Laplacian, and a way to
// give every boundary face its own exact Dirichlet value (needed because
// production FixedValue is a single constant -- see makeExactBoundaries
// below for why and how).

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace cfd::test {

// --- Manufactured fields (exact, polynomial up to degree 2) --------------
// These are the exactness-test fields from the spec: linear fields should
// be reproduced by these operators to near machine precision, and the
// quadratic field's flux at any face midpoint is *exactly* its analytical
// derivative there (a property of central differencing on a uniform
// Cartesian grid), so the operators reproduce it exactly too. That makes
// them excellent correctness probes, but useless for a grid-refinement
// *order* study (see phiSmooth below): the error is already at floating-
// point noise on every grid, so log(E_h/E_h2) is meaningless.

inline Real phiX(const Vector2& p) { return p.x; }
inline Real phiY(const Vector2& p) { return p.y; }
inline Real phiQuadratic(const Vector2& p) { return (p.x * p.x) + (p.y * p.y); }
inline Vector2 gradQuadratic(const Vector2& p) { return Vector2{2.0 * p.x, 2.0 * p.y}; }

// --- A genuinely smooth (non-polynomial) field, for observed-order study -
// phi = sin(pi x) cos(pi y) has real (h^2-scaling) truncation error under
// this scheme, so refining the grid produces a real, meaningful observed
// convergence order -- unlike the polynomial fields above.
inline Real phiSmooth(const Vector2& p) {
  return std::sin(constants::pi * p.x) * std::cos(constants::pi * p.y);
}
inline Vector2 gradSmooth(const Vector2& p) {
  const Real pi = constants::pi;
  return Vector2{pi * std::cos(pi * p.x) * std::cos(pi * p.y),
                 -pi * std::sin(pi * p.x) * std::sin(pi * p.y)};
}
inline Real laplacianSmooth(const Vector2& p) {
  return -2.0 * constants::pi * constants::pi * phiSmooth(p);
}

// --- Per-face exact boundary values ---------------------------------------
// FixedValue is a single uniform value, but a manufactured field's exact
// boundary value varies along a patch (e.g. phi=x^2+y^2 differs at every
// point of the "top" patch). Rather than adding a spatially-varying BC
// type to production code (explicitly out of scope -- see TODO.md), we
// rebuild the mesh's boundary patches as one singleton patch per face,
// then assign each its own exact FixedValue. This uses only existing,
// already-verified production APIs.
inline cfd::mesh::Mesh perFaceBoundaryMesh(Index nx, Index ny, Real lengthX, Real lengthY) {
  const cfd::mesh::Mesh base = cfd::mesh::MeshGeometry::createCartesian2D(nx, ny, lengthX, lengthY);

  std::vector<cfd::mesh::Cell> cells = base.cells();
  std::vector<cfd::mesh::Face> faces = base.faces();

  std::vector<cfd::mesh::BoundaryPatch> patches;
  for (const auto& face : faces) {
    if (face.isBoundary()) {
      patches.emplace_back("b" + std::to_string(face.id()), std::vector<Index>{face.id()});
    }
  }

  return cfd::mesh::Mesh(std::move(cells), std::move(faces), std::move(patches));
}

template <typename PhiFunction>
cfd::boundary::BoundaryConditionSet makeExactBoundaries(const cfd::mesh::Mesh& mesh,
                                                        PhiFunction phi) {
  cfd::boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    const Real value = phi(mesh.face(faceId).centroid());
    bcs.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(value));
  }
  return bcs;
}

// --- Error metrics ---------------------------------------------------------

// Volume-weighted L2 error over all cells.
template <typename NumericField, typename ExactFunction>
Real l2CellError(const cfd::mesh::Mesh& mesh, const NumericField& numeric, ExactFunction exact) {
  Real weightedSquaredError = 0.0;
  Real totalVolume = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Real error = numeric[cell.id()] - exact(cell.centroid());
    weightedSquaredError += error * error * cell.volume();
    totalVolume += cell.volume();
  }
  return std::sqrt(weightedSquaredError / totalVolume);
}

// Volume-weighted L2 error for a vector field (magnitude of the
// difference vector).
template <typename ExactVectorFunction>
Real l2CellErrorVector(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& numeric,
                       ExactVectorFunction exact) {
  Real weightedSquaredError = 0.0;
  Real totalVolume = 0.0;
  for (const auto& cell : mesh.cells()) {
    const Vector2 error = numeric[cell.id()] - exact(cell.centroid());
    weightedSquaredError += dot(error, error) * cell.volume();
    totalVolume += cell.volume();
  }
  return std::sqrt(weightedSquaredError / totalVolume);
}

// Observed order p from two refinement levels (grid spacing halved):
// p = log(E_h / E_h2) / log(2).
inline Real observedOrder(Real errorCoarse, Real errorFine) {
  return std::log(errorCoarse / errorFine) / std::log(2.0);
}

}  // namespace cfd::test

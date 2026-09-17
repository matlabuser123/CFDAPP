// P12-MESH-001: cell gradients next to Neumann-type (not value-
// prescribing) boundary faces met obliquely by their grid lines -- a
// structured_quad mesh whose vertical lines are tilted at the walls. For a
// linear field satisfying the boundary conditions, both reconstructions
// must return its exact gradient in every cell, wall-adjacent cells
// included. Before P12-MESH-001 the Neumann face value was taken at the
// face centroid with the straight-line owner distance, ignoring the
// tangential variation of the field between the owner and the face -- an
// O(1) gradient error in the wall cells that stalled a conduction
// solution's error at ~4e-2 of 20 K under refinement
// (results/p12-mesh-001/summary.md). Verification tests (exactness for
// linear fields), not self-consistency.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::FixedValue;
using cfd::discretization::GradientScheme;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// Unit square, vertical grid lines tilted at both walls by up to ~35
// degrees (x-displacement only, so the horizontal rows stay straight).
Mesh tiltedMesh(Index n) {
  const Real pi = std::acos(-1.0);
  std::vector<Vector2> vertices;
  for (Index j = 0; j <= n; ++j) {
    for (Index i = 0; i <= n; ++i) {
      const Real xi = static_cast<Real>(i) / static_cast<Real>(n);
      const Real eta = static_cast<Real>(j) / static_cast<Real>(n);
      Real x = xi + (0.1 * std::sin(pi * xi) * std::sin(2.0 * pi * eta));
      if (i == 0) x = 0.0;
      if (i == n) x = 1.0;
      vertices.push_back(Vector2{x, eta});
    }
  }
  return MeshGeometry::createStructuredQuad2D(n, n, vertices);
}

cfd::fields::ScalarField linearField(const Mesh& mesh, Vector2 gradient) {
  cfd::fields::ScalarField field(mesh.numberOfCells(), 0.0);
  for (const auto& cell : mesh.cells()) field[cell.id()] = dot(gradient, cell.centroid());
  return field;
}

Real maxGradientError(const Mesh& mesh, const cfd::fields::VectorField& gradient, Vector2 exact) {
  Real worst = 0.0;
  for (const auto& cell : mesh.cells()) {
    worst = std::max(worst, magnitude(gradient[cell.id()] - exact));
  }
  return worst;
}

}  // namespace

// phi = x: Dirichlet phi = 0 / 1 on left / right, zero normal gradient on
// the (obliquely met) bottom and top walls.
TEST(ObliqueNeumannGradient, ZeroGradientWallsLinearFieldIsExact) {
  const Mesh mesh = tiltedMesh(12);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(1.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  const auto phi = linearField(mesh, Vector2{1.0, 0.0});

  const auto leastSquares = gradient(mesh, phi, boundaries, GradientScheme::LeastSquares);
  EXPECT_LT(maxGradientError(mesh, leastSquares, Vector2{1.0, 0.0}), 1e-12);
  const auto greenGauss = gradient(mesh, phi, boundaries, GradientScheme::GreenGauss);
  std::printf("zero-gradient walls: max |grad error| least_squares %.3e, green_gauss %.3e\n",
              maxGradientError(mesh, leastSquares, Vector2{1.0, 0.0}),
              maxGradientError(mesh, greenGauss, Vector2{1.0, 0.0}));
  // Green-Gauss reaches the exact face values by fixed-point sweeps
  // (kGreenGaussSkewCorrectionSweeps = 4): measured 3.3e-7; bound 1e-5.
  EXPECT_LT(maxGradientError(mesh, greenGauss, Vector2{1.0, 0.0}), 1e-5);
}

// phi = x + 2y with every boundary Neumann: nonzero prescribed normal
// gradients (-1 / +1 on left / right, -2 / +2 on bottom / top).
TEST(ObliqueNeumannGradient, NonZeroPrescribedGradientsLinearFieldIsExact) {
  const Mesh mesh = tiltedMesh(12);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedGradient>(-1.0));
  boundaries.set(mesh, "right", std::make_unique<FixedGradient>(1.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(-2.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(2.0));
  const auto phi = linearField(mesh, Vector2{1.0, 2.0});

  const auto leastSquares = gradient(mesh, phi, boundaries, GradientScheme::LeastSquares);
  EXPECT_LT(maxGradientError(mesh, leastSquares, Vector2{1.0, 2.0}), 1e-12);
  const auto greenGauss = gradient(mesh, phi, boundaries, GradientScheme::GreenGauss);
  std::printf("prescribed gradients: max |grad error| least_squares %.3e, green_gauss %.3e\n",
              maxGradientError(mesh, leastSquares, Vector2{1.0, 2.0}),
              maxGradientError(mesh, greenGauss, Vector2{1.0, 2.0}));
  // Measured 1.7e-7 (see above); bound 1e-5.
  EXPECT_LT(maxGradientError(mesh, greenGauss, Vector2{1.0, 2.0}), 1e-5);
}

// On a Cartesian mesh (no oblique face) the same conditions still give the
// exact gradient -- the pre-existing treatment, unchanged.
TEST(ObliqueNeumannGradient, CartesianMeshUnchangedAndExact) {
  const Mesh mesh = MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<FixedValue>(0.0));
  boundaries.set(mesh, "right", std::make_unique<FixedValue>(1.0));
  boundaries.set(mesh, "bottom", std::make_unique<FixedGradient>(0.0));
  boundaries.set(mesh, "top", std::make_unique<FixedGradient>(0.0));
  const auto phi = linearField(mesh, Vector2{1.0, 0.0});
  for (const GradientScheme scheme : {GradientScheme::LeastSquares, GradientScheme::GreenGauss}) {
    EXPECT_LT(maxGradientError(mesh, gradient(mesh, phi, boundaries, scheme), Vector2{1.0, 0.0}),
              1e-12);
  }
}

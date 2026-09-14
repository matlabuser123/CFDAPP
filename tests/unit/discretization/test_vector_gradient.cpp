// P2-TURB-004: cfd::discretization::computeVelocityGradient -- the plain
// Green-Gauss vector-field gradient turbulent production needs (section
// 6's own mandatory hand-derived test lives here for the gradient half;
// TurbulenceProduction tests build on top of these same fixtures).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "DistortedMesh.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::discretization::computeVelocityGradient;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// Same per-face-patch trick as
// tests/unit/physics/test_momentum_diffusion.cpp's
// InteriorCellsMatchKnownQuadraticLaplacian: gives every boundary face
// its own patch so a manufactured field's exact value can be assigned
// face-by-face, making even the boundary-adjacent cells' gradient exact
// for a field with no curvature (a linear field, as required here).
Mesh makePerFaceMesh(const Mesh& source) {
  std::vector<cfd::mesh::Cell> cells = source.cells();
  std::vector<cfd::mesh::Face> faces = source.faces();
  std::vector<cfd::mesh::BoundaryPatch> patches;
  for (const auto& face : faces) {
    if (face.isBoundary()) {
      patches.emplace_back("b" + std::to_string(face.id()), std::vector<Index>{face.id()});
    }
  }
  return Mesh(std::move(cells), std::move(faces), std::move(patches));
}

}  // namespace

TEST(VectorGradientTest, LinearShearFieldIsExactEverywhere) {
  // P2-TURB-004 section 6: u = a*y, v = 0 -> du/dy = a exactly, every
  // other component exactly zero, at EVERY cell (interior and boundary
  // alike -- see this file's own header comment on why the per-face-
  // patch mesh makes that provable even with the plain, unpaired
  // Green-Gauss formula VectorGradient.cpp uses).
  const Mesh base = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh mesh = makePerFaceMesh(base);
  const Real a = 3.0;
  const auto u = [a](const Vector2& p) { return Vector2{a * p.y, 0.0}; };

  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    boundaries.set(mesh, patch.name(),
                   std::make_unique<MovingWall>(u(mesh.face(faceId).centroid())));
  }

  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) velocity[cell.id()] = u(cell.centroid());

  const auto gradient = computeVelocityGradient(mesh, velocity, boundaries);
  ASSERT_EQ(gradient.gradU.size(), mesh.numberOfCells());
  ASSERT_EQ(gradient.gradV.size(), mesh.numberOfCells());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(gradient.gradU[i].x, 0.0, 1e-10) << "cell " << i;
    EXPECT_NEAR(gradient.gradU[i].y, a, 1e-10) << "cell " << i;
    EXPECT_NEAR(gradient.gradV[i].x, 0.0, 1e-10) << "cell " << i;
    EXPECT_NEAR(gradient.gradV[i].y, 0.0, 1e-10) << "cell " << i;
  }
}

TEST(VectorGradientTest, UniformVelocityGivesZeroGradientEverywhere) {
  // Section 35's "zero-shear" premise, at the gradient level: a uniform
  // field has grad(U) = 0 everywhere, exactly.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  const Vector2 uniform{2.0, -1.5};
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(uniform));
  }
  const VectorField velocity(mesh.numberOfCells(), uniform);

  const auto gradient = computeVelocityGradient(mesh, velocity, boundaries);
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(gradient.gradU[i].x, 0.0, 1e-12) << "cell " << i;
    EXPECT_NEAR(gradient.gradU[i].y, 0.0, 1e-12) << "cell " << i;
    EXPECT_NEAR(gradient.gradV[i].x, 0.0, 1e-12) << "cell " << i;
    EXPECT_NEAR(gradient.gradV[i].y, 0.0, 1e-12) << "cell " << i;
  }
}

// P12-NUM-003: the LeastSquares option (reusing Gradient.hpp's
// solveLeastSquaresGradient) is exact for a general linear velocity field
// on a DISTORTED mesh, at every cell including boundary-adjacent ones; the
// (skewness-corrected) GreenGauss default is close (~1e-9) but not exact
// (measured, printed). The explicit GreenGauss call equals the
// default-argument call bit-for-bit.
TEST(VectorGradientTest, LeastSquaresIsExactForLinearFieldOnDistortedMesh) {
  const Mesh mesh = makePerFaceMesh(cfd::test::createDistortedQuad2D(9, 9, 1.0, 1.0, 0.4 / 9.0));
  const auto u = [](const Vector2& p) {
    return Vector2{(2.0 * p.x) + (3.0 * p.y) + 1.0, (-1.5 * p.x) + (0.5 * p.y) - 4.0};
  };
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    boundaries.set(mesh, patch.name(),
                   std::make_unique<MovingWall>(u(mesh.face(faceId).centroid())));
  }
  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) velocity[cell.id()] = u(cell.centroid());

  const auto ls = computeVelocityGradient(mesh, velocity, boundaries,
                                          cfd::discretization::GradientScheme::LeastSquares);
  const auto gg = computeVelocityGradient(mesh, velocity, boundaries);
  const auto ggExplicit = computeVelocityGradient(mesh, velocity, boundaries,
                                                  cfd::discretization::GradientScheme::GreenGauss);
  Real maxLsError = 0.0;
  Real maxGgError = 0.0;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    maxLsError = std::max({maxLsError, std::abs(ls.gradU[i].x - 2.0), std::abs(ls.gradU[i].y - 3.0),
                           std::abs(ls.gradV[i].x + 1.5), std::abs(ls.gradV[i].y - 0.5)});
    maxGgError = std::max({maxGgError, std::abs(gg.gradU[i].x - 2.0), std::abs(gg.gradU[i].y - 3.0),
                           std::abs(gg.gradV[i].x + 1.5), std::abs(gg.gradV[i].y - 0.5)});
    EXPECT_EQ(gg.gradU[i].x, ggExplicit.gradU[i].x);
    EXPECT_EQ(gg.gradV[i].y, ggExplicit.gradV[i].y);
  }
  std::printf(
      "\nVelocity gradient, linear field, distorted 9x9 (0.4h): max component error "
      "GreenGauss %.4g, LeastSquares %.3g\n",
      maxGgError, maxLsError);
  EXPECT_LT(maxLsError, 1e-11);
  EXPECT_LT(maxGgError, 1e-8);
  EXPECT_GT(maxGgError, maxLsError);
}

TEST(VectorGradientTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(Vector2{0.0, 0.0}));
  }
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});

  EXPECT_THROW((void)computeVelocityGradient(mesh, velocity, boundaries), InvalidArgumentError);
}

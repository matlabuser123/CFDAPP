#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

TEST(InterpolationTest, ConstantFieldIsPreservedExactly) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(2, 2, 1.0, 1.0);
  const auto boundaries =
      cfd::test::makeExactBoundaries(mesh, [](const cfd::Vector2&) { return 3.0; });

  const ScalarField field(mesh.numberOfCells(), 3.0);
  const auto faceValues = cfd::discretization::interpolate(mesh, field, boundaries);

  for (cfd::Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    EXPECT_DOUBLE_EQ(faceValues[faceId], 3.0);
  }
}

TEST(InterpolationTest, LinearFieldPhiXIsExactAtEveryFace) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiX(cell.centroid());
  }

  const auto faceValues = cfd::discretization::interpolate(mesh, field, boundaries);
  for (cfd::Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Real exact = cfd::test::phiX(mesh.face(faceId).centroid());
    EXPECT_NEAR(faceValues[faceId], exact, 1e-12);
  }
}

TEST(InterpolationTest, LinearFieldPhiYIsExactAtEveryFace) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiY);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiY(cell.centroid());
  }

  const auto faceValues = cfd::discretization::interpolate(mesh, field, boundaries);
  for (cfd::Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    const Real exact = cfd::test::phiY(mesh.face(faceId).centroid());
    EXPECT_NEAR(faceValues[faceId], exact, 1e-12);
  }
}

TEST(InterpolationTest, BoundaryFaceUsesFixedValueNotOwnerValue) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(5.0));
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(5.0));
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedValue>(5.0));
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedValue>(5.0));

  const ScalarField field(mesh.numberOfCells(), 100.0);  // owner value far from the BC
  const auto faceValues = cfd::discretization::interpolate(mesh, field, boundaries);

  for (cfd::Index faceId = 0; faceId < mesh.numberOfFaces(); ++faceId) {
    EXPECT_DOUBLE_EQ(faceValues[faceId], 5.0);
  }
}

// ===========================================================================
// P12-NUM-003: skewness-corrected internal-face interpolation.
// ===========================================================================

// Linear field phi = 2x + 3y + 5 on a distorted (skewed) mesh, with its
// gradient reconstructed by least squares (exact for a linear field --
// P12-NUM-002): the skew-corrected face value must equal the exact value
// at every internal face centroid to round-off, whereas plain distance-
// weighted interpolation is measurably wrong on skewed faces. Measured
// maxima are printed as evidence.
TEST(InterpolationTest, SkewCorrectedInterpolationIsExactForLinearFieldOnSkewedMesh) {
  const auto phi = [](const cfd::Vector2& p) { return (2.0 * p.x) + (3.0 * p.y) + 5.0; };
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(
      cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.3 / 10.0));
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, phi);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = phi(cell.centroid());
  }
  const auto grad = cfd::discretization::gradient(
      mesh, field, boundaries, cfd::discretization::GradientScheme::LeastSquares);

  Real maxPlainError = 0.0;
  Real maxCorrectedError = 0.0;
  Real maxSkewness = 0.0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      continue;
    }
    const Real exact = phi(face.centroid());
    const Real plain = cfd::discretization::interpolateInternalFace(mesh, face, field);
    const Real corrected =
        cfd::discretization::interpolateInternalFaceSkewCorrected(mesh, face, field, grad);
    maxPlainError = std::max(maxPlainError, std::abs(plain - exact));
    maxCorrectedError = std::max(maxCorrectedError, std::abs(corrected - exact));
    maxSkewness = std::max(maxSkewness, *MeshGeometry::skewness(mesh, face));
  }
  std::printf(
      "\nSkew-corrected interpolation, phi=2x+3y+5, distorted 10x10 (0.3h, max skewness %.4g):"
      " max face error plain %.4g, skew-corrected %.3g\n",
      maxSkewness, maxPlainError, maxCorrectedError);
  EXPECT_LT(maxCorrectedError, 1e-12);
  EXPECT_GT(maxPlainError, 1e-4);
}

// On an unskewed (Cartesian) mesh the skew vector is zero, so the
// corrected value reduces to the plain distance-weighted one.
TEST(InterpolationTest, SkewCorrectedInterpolationMatchesPlainOnCartesianMesh) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(6, 6, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiSmooth);
  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiSmooth(cell.centroid());
  }
  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      continue;
    }
    EXPECT_NEAR(cfd::discretization::interpolateInternalFaceSkewCorrected(mesh, face, field, grad),
                cfd::discretization::interpolateInternalFace(mesh, face, field), 1e-14);
  }
}

// Degenerate crossing (Sf perpendicular to d): falls back to the plain
// interpolation, never NaN/Inf; boundary faces are rejected.
TEST(InterpolationTest, SkewCorrectedInterpolationFallsBackOnDegenerateFace) {
  std::vector<cfd::mesh::Cell> cells;
  cells.emplace_back(0, cfd::Vector2{0.0, 0.0}, 1.0);
  cells.emplace_back(1, cfd::Vector2{1.0, 0.0}, 1.0);
  std::vector<cfd::mesh::Face> faces;
  faces.emplace_back(0, 0, 1, cfd::Vector2{0.5, 0.2}, cfd::Vector2{0.0, 1.0});
  cells[0].addFace(0);
  cells[1].addFace(0);
  const Mesh mesh(std::move(cells), std::move(faces), {});
  ScalarField field(2);
  field[0] = 1.0;
  field[1] = 3.0;
  const cfd::fields::VectorField grad(2, cfd::Vector2{2.0, 0.0});
  const Real value =
      cfd::discretization::interpolateInternalFaceSkewCorrected(mesh, mesh.face(0), field, grad);
  EXPECT_TRUE(std::isfinite(value));
  EXPECT_EQ(value, cfd::discretization::interpolateInternalFace(mesh, mesh.face(0), field));

  const Mesh cartesian = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const ScalarField one(1, 1.0);
  const cfd::fields::VectorField zeroGrad(1, cfd::Vector2{0.0, 0.0});
  EXPECT_THROW((void)cfd::discretization::interpolateInternalFaceSkewCorrected(
                   cartesian, cartesian.face(0), one, zeroGrad),
               cfd::InvalidArgumentError);
}

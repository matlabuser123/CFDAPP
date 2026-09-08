#include <gtest/gtest.h>

#include "ManufacturedFields.hpp"
#include "cfd/discretization/Gradient.hpp"

using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

TEST(GradientTest, ConstantFieldHasZeroGradient) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto boundaries =
      cfd::test::makeExactBoundaries(mesh, [](const cfd::Vector2&) { return 7.0; });
  const ScalarField field(mesh.numberOfCells(), 7.0);

  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(grad[cell.id()].x, 0.0, 1e-10);
    EXPECT_NEAR(grad[cell.id()].y, 0.0, 1e-10);
  }
}

TEST(GradientTest, PhiXGivesGradientOneZero) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiX(cell.centroid());
  }

  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(grad[cell.id()].x, 1.0, 1e-10);
    EXPECT_NEAR(grad[cell.id()].y, 0.0, 1e-10);
  }
}

TEST(GradientTest, PhiYGivesGradientZeroOne) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiY);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiY(cell.centroid());
  }

  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(grad[cell.id()].x, 0.0, 1e-10);
    EXPECT_NEAR(grad[cell.id()].y, 1.0, 1e-10);
  }
}

TEST(GradientTest, QuadraticFieldMatchesAnalyticalGradient) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(16, 16, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto grad = cfd::discretization::gradient(mesh, field, boundaries);
  const Real error = cfd::test::l2CellErrorVector(mesh, grad, cfd::test::gradQuadratic);
  EXPECT_LT(error, 1e-9);
}

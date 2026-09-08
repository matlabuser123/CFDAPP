#include <gtest/gtest.h>

#include "ManufacturedFields.hpp"
#include "cfd/discretization/Laplacian.hpp"

using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

TEST(LaplacianTest, PhiXHasZeroLaplacian) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiX);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiX(cell.centroid());
  }

  const auto lap = cfd::discretization::laplacian(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(lap[cell.id()], 0.0, 1e-9);
  }
}

TEST(LaplacianTest, PhiYHasZeroLaplacian) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiY);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiY(cell.centroid());
  }

  const auto lap = cfd::discretization::laplacian(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(lap[cell.id()], 0.0, 1e-9);
  }
}

TEST(LaplacianTest, QuadraticFieldGivesFour) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto lap = cfd::discretization::laplacian(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(lap[cell.id()], 4.0, 1e-9);
  }
}

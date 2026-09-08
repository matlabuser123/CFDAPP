#include <gtest/gtest.h>

#include "ManufacturedFields.hpp"
#include "cfd/discretization/Diffusion.hpp"
#include "cfd/discretization/Laplacian.hpp"

using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;

TEST(DiffusionTest, EqualsGammaTimesLaplacianForConstantGamma) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const Real gamma = 2.0;
  const auto diff = cfd::discretization::diffusion(mesh, field, gamma, boundaries);
  const auto lap = cfd::discretization::laplacian(mesh, field, boundaries);

  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(diff[cell.id()], gamma * lap[cell.id()], 1e-9);
  }
}

TEST(DiffusionTest, QuadraticFieldWithGammaHalf) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto diff = cfd::discretization::diffusion(mesh, field, 0.5, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(diff[cell.id()], 2.0, 1e-9);  // 0.5 * 4
  }
}

TEST(DiffusionTest, QuadraticFieldWithGammaTwo) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = cfd::test::makeExactBoundaries(mesh, cfd::test::phiQuadratic);

  ScalarField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = cfd::test::phiQuadratic(cell.centroid());
  }

  const auto diff = cfd::discretization::diffusion(mesh, field, 2.0, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(diff[cell.id()], 8.0, 1e-9);
  }
}

TEST(DiffusionTest, ZeroFluxBoundaryConservesGlobally) {
  // Constant field -> zero gradient everywhere -> internal fluxes cancel
  // pairwise (face-once conservation) and there is no boundary flux, so
  // the volume-weighted sum must be exactly zero.
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto boundaries =
      cfd::test::makeExactBoundaries(mesh, [](const cfd::Vector2&) { return 5.0; });
  const ScalarField field(mesh.numberOfCells(), 5.0);

  const auto diff = cfd::discretization::diffusion(mesh, field, 3.0, boundaries);
  Real sum = 0.0;
  for (const auto& cell : mesh.cells()) {
    sum += diff[cell.id()] * cell.volume();
  }
  EXPECT_NEAR(sum, 0.0, 1e-10);
}

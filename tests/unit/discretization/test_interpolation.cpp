#include <gtest/gtest.h>

#include <memory>

#include "ManufacturedFields.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/discretization/Interpolation.hpp"
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

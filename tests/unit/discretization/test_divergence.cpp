#include <gtest/gtest.h>

#include <memory>

#include "ManufacturedFields.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/Divergence.hpp"
#include "cfd/discretization/Interpolation.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Real;
using cfd::Vector2;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

// U=(x,y) etc. vary per point, so (like the scalar manufactured tests)
// each singleton boundary patch gets its own exact prescribed velocity
// via MovingWall (a plain VectorBoundaryCondition -- semantically this is
// a manufactured Dirichlet velocity BC, not a literal moving wall).
cfd::boundary::BoundaryConditionSet makeExactVectorBoundaries(const Mesh& mesh,
                                                              Vector2 (*u)(const Vector2&)) {
  cfd::boundary::BoundaryConditionSet bcs;
  for (const auto& patch : mesh.boundaryPatches()) {
    const cfd::Index faceId = patch.faceIds().front();
    bcs.set(mesh, patch.name(),
            std::make_unique<cfd::boundary::MovingWall>(u(mesh.face(faceId).centroid())));
  }
  return bcs;
}

Vector2 uLinear(const Vector2& p) { return Vector2{p.x, p.y}; }
Vector2 uRotational(const Vector2& p) { return Vector2{-p.y, p.x}; }

}  // namespace

TEST(DivergenceTest, ConstantVectorFieldHasZeroDivergence) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(4, 4, 1.0, 1.0);
  const auto boundaries =
      makeExactVectorBoundaries(mesh, [](const Vector2&) { return Vector2{2.0, -1.0}; });
  const VectorField field(mesh.numberOfCells(), Vector2{2.0, -1.0});

  const auto div = cfd::discretization::divergence(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(div[cell.id()], 0.0, 1e-10);
  }
}

TEST(DivergenceTest, ULinearGivesDivergenceTwo) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = makeExactVectorBoundaries(mesh, uLinear);

  VectorField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = uLinear(cell.centroid());
  }

  const auto div = cfd::discretization::divergence(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(div[cell.id()], 2.0, 1e-9);
  }
}

TEST(DivergenceTest, RotationalFieldIsDivergenceFree) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(8, 8, 1.0, 1.0);
  const auto boundaries = makeExactVectorBoundaries(mesh, uRotational);

  VectorField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = uRotational(cell.centroid());
  }

  const auto div = cfd::discretization::divergence(mesh, field, boundaries);
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(div[cell.id()], 0.0, 1e-9);
  }
}

TEST(DivergenceTest, GlobalDivergenceTheoremHolds) {
  // A structural conservation identity, true regardless of whether the
  // boundary values are physically "correct": internal-face
  // contributions cancel pairwise (each face's Uf used once, with
  // opposite sign, by its two cells), leaving exactly the boundary flux.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);

  cfd::boundary::BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "right", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
  boundaries.set(mesh, "top", std::make_unique<cfd::boundary::MovingWall>(Vector2{1.0, 0.0}));

  VectorField field(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    field[cell.id()] = uLinear(cell.centroid());
  }

  const auto div = cfd::discretization::divergence(mesh, field, boundaries);

  Real volumeWeightedSum = 0.0;
  for (const auto& cell : mesh.cells()) {
    volumeWeightedSum += div[cell.id()] * cell.volume();
  }

  Real boundaryFlux = 0.0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      const Vector2 uf = cfd::discretization::interpolateFace(mesh, face, field, boundaries);
      boundaryFlux += cfd::dot(uf, face.areaVector());
    }
  }

  EXPECT_NEAR(volumeWeightedSum, boundaryFlux, 1e-9);
}

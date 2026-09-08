#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::MovingWall;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::VelocityComponent;

namespace {

BoundaryConditionSet makeConstantVelocityBoundaries(const Mesh& mesh, Vector2 velocity) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(velocity));
  }
  return boundaries;
}

bool cellTouchesBoundary(const Mesh& mesh, const cfd::mesh::Cell& cell) {
  for (const Index faceId : cell.faceIds()) {
    if (mesh.face(faceId).isBoundary()) return true;
  }
  return false;
}

}  // namespace

TEST(MomentumDiffusionTest, ConstantVelocityGivesZeroContributionEverywhere) {
  // TODO.md section 29/54: a constant field with a matching Dirichlet
  // boundary has zero gradient everywhere, so the diffusion contribution
  // (A*u - b) must be exactly zero in every cell -- this is the
  // "constant field preservation" identity applied to the assembled
  // system rather than the evaluate-style operator.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Real value = 7.0;
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{value, 0.0});
  const Index n = mesh.numberOfCells();

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const VectorField velocity(n, Vector2{value, 0.0});
  assembleDiffusionContribution(mesh, /*dynamicViscosity=*/2.0, velocity, boundaries,
                                VelocityComponent::U, builder, rhs);

  const auto matrix = builder.build();
  const Vector uExact(n, value);
  const Vector residual = matrix.multiply(uExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-10);
  }
}

TEST(MomentumDiffusionTest, InteriorCellsMatchKnownQuadraticLaplacian) {
  // TODO.md section 54: u=x^2+y^2 -> Laplacian(u)=4 exactly, so for
  // interior cells (where the boundary-treatment asymmetry documented in
  // Diffusion.cpp does not apply) A*u_exact - b == -mu*4*V_P: this
  // equation's diffusion term enters as -mu*Laplacian(u) (see
  // MomentumEquation.hpp's governing-equation comment).
  const Mesh mesh = MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0);
  const auto phi = [](const Vector2& p) { return (p.x * p.x) + (p.y * p.y); };
  BoundaryConditionSet boundaries;
  // Approximate per-patch exact value isn't possible with one MovingWall
  // per side for a quadratic field; use the cell-centered field itself
  // for boundary velocity via a per-face patch, matching the
  // discretization tests' ManufacturedFields pattern.
  std::vector<cfd::mesh::Cell> cells = mesh.cells();
  std::vector<cfd::mesh::Face> faces = mesh.faces();
  std::vector<cfd::mesh::BoundaryPatch> patches;
  for (const auto& face : faces) {
    if (face.isBoundary()) {
      patches.emplace_back("b" + std::to_string(face.id()), std::vector<Index>{face.id()});
    }
  }
  const Mesh perFaceMesh(std::move(cells), std::move(faces), std::move(patches));
  for (const auto& patch : perFaceMesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    const Real value = phi(perFaceMesh.face(faceId).centroid());
    boundaries.set(perFaceMesh, patch.name(), std::make_unique<MovingWall>(Vector2{value, 0.0}));
  }

  const Index n = perFaceMesh.numberOfCells();
  VectorField velocity(n);
  for (const auto& cell : perFaceMesh.cells()) {
    velocity[cell.id()] = Vector2{phi(cell.centroid()), 0.0};
  }

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const Real mu = 2.0;
  assembleDiffusionContribution(perFaceMesh, mu, velocity, boundaries, VelocityComponent::U,
                                builder, rhs);
  const auto matrix = builder.build();

  Vector uExact(n);
  for (const auto& cell : perFaceMesh.cells()) uExact[cell.id()] = phi(cell.centroid());
  const Vector residual = matrix.multiply(uExact) - rhs;

  for (const auto& cell : perFaceMesh.cells()) {
    if (cellTouchesBoundary(perFaceMesh, cell)) continue;
    EXPECT_NEAR(residual[cell.id()], -mu * 4.0 * cell.volume(), 1e-9);
  }
}

TEST(MomentumDiffusionTest, InternalFaceCoefficientsAreSymmetric) {
  // Cells 0 (bottom-left) and 1 (its right neighbor) share an internal
  // face on this 3x3 mesh -- A(0,1) and A(1,0) should be equal
  // (TODO.md section 10's "equal/opposite" neighbor-row contribution).
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const VectorField velocity(n, Vector2{0.0, 0.0});
  assembleDiffusionContribution(mesh, 1.0, velocity, boundaries, VelocityComponent::U, builder,
                                rhs);
  const auto matrix = builder.build();

  Vector e0(n, 0.0);
  e0[0] = 1.0;
  Vector e1(n, 0.0);
  e1[1] = 1.0;
  const Real a10 = matrix.multiply(e0)[1];  // A(1,0)
  const Real a01 = matrix.multiply(e1)[0];  // A(0,1)
  EXPECT_NEAR(a01, a10, 1e-12);
  EXPECT_LT(a01, 0.0);  // off-diagonal diffusion coefficients are negative
  EXPECT_TRUE(matrix.allFinite());
}

TEST(MomentumDiffusionTest, DiagonalIsFinitePositiveAndNonzero) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const Index n = mesh.numberOfCells();

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  const VectorField velocity(n, Vector2{0.0, 0.0});
  assembleDiffusionContribution(mesh, 1.5, velocity, boundaries, VelocityComponent::U, builder,
                                rhs);
  const auto matrix = builder.build();

  for (Index row = 0; row < n; ++row) {
    const Real aP = matrix.diagonal(row);
    EXPECT_TRUE(std::isfinite(aP));
    EXPECT_GT(aP, 0.0);
  }
}

TEST(MomentumDiffusionTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto boundaries = makeConstantVelocityBoundaries(mesh, Vector2{0.0, 0.0});
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);

  EXPECT_THROW(assembleDiffusionContribution(mesh, 1.0, velocity, boundaries, VelocityComponent::U,
                                             builder, rhs),
               InvalidArgumentError);
}

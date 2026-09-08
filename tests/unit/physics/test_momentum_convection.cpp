#include <gtest/gtest.h>

#include <memory>

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
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
using cfd::boundary::Inlet;
using cfd::boundary::MovingWall;
using cfd::boundary::Outlet;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleConvectionContribution;
using cfd::physics::VelocityComponent;

namespace {

Mesh makeChannelMesh() { return MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0); }

BoundaryConditionSet makeChannelBoundaries(const Mesh& mesh, Vector2 inletVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(inletVelocity));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<MovingWall>(Vector2{0.0, 0.0}));
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{0.0, 0.0}));
  return boundaries;
}

}  // namespace

TEST(MomentumConvectionTest, PositiveInternalFluxSelectsOwnerNegativeSelectsNeighbor) {
  // TODO.md section 26/31: F>=0 -> owner is upwind (A(P,P) gets F, not
  // A(P,N)); F<0 -> neighbor is upwind.
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});

  Index internalFaceId = 0;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) internalFaceId = face.id();
  }

  {
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    massFlux[internalFaceId] = 5.0;  // F>=0: owner (cell 0) upwind.
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                   builder, rhs);
    const auto matrix = builder.build();
    Vector e0(n, 0.0);
    e0[0] = 1.0;
    EXPECT_NEAR(matrix.multiply(e0)[0], 5.0, 1e-12);  // A(0,0) += F
    Vector e1(n, 0.0);
    e1[1] = 1.0;
    EXPECT_NEAR(matrix.multiply(e1)[0], 0.0, 1e-12);  // A(0,1) untouched
  }
  {
    SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
    massFlux[internalFaceId] = -5.0;  // F<0: neighbor (cell 1) upwind.
    SparseMatrixBuilder builder(n, n);
    Vector rhs(n, 0.0);
    assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                   builder, rhs);
    const auto matrix = builder.build();
    Vector e1(n, 0.0);
    e1[1] = 1.0;
    EXPECT_NEAR(matrix.multiply(e1)[0], -5.0, 1e-12);  // A(0,1) += F (negative)
    Vector e0(n, 0.0);
    e0[0] = 1.0;
    EXPECT_NEAR(matrix.multiply(e0)[0], 0.0, 1e-12);  // A(0,0) untouched
  }
}

TEST(MomentumConvectionTest, BoundaryOutflowUsesOwnerUnknownNotBoundaryValue) {
  // TODO.md section 27: outgoing flux at a boundary uses the owner/
  // interior value (an unknown -> matrix diagonal), not the boundary
  // condition's value.
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});

  Index rightFaceId = 0;
  for (const Index faceId : mesh.boundaryPatch("right").faceIds()) rightFaceId = faceId;
  const Index ownerCell = mesh.face(rightFaceId).owner();

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[rightFaceId] = 4.0;  // outflow

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                 builder, rhs);
  const auto matrix = builder.build();
  Vector e(n, 0.0);
  e[ownerCell] = 1.0;
  EXPECT_NEAR(matrix.multiply(e)[ownerCell], 4.0, 1e-12);
  EXPECT_NEAR(rhs[ownerCell], 0.0, 1e-12);
}

TEST(MomentumConvectionTest, BoundaryInflowUsesBoundaryValueOnRhs) {
  // TODO.md section 27: incoming flux at a boundary must use the
  // prescribed boundary value -- a known number, so it lands entirely on
  // the RHS, not the matrix.
  const Mesh mesh = makeChannelMesh();
  const Vector2 inletVelocity{1.0, 0.0};
  const auto boundaries = makeChannelBoundaries(mesh, inletVelocity);
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});

  Index leftFaceId = 0;
  for (const Index faceId : mesh.boundaryPatch("left").faceIds()) leftFaceId = faceId;
  const Index ownerCell = mesh.face(leftFaceId).owner();

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  massFlux[leftFaceId] = -3.0;  // inflow

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                 builder, rhs);
  const auto matrix = builder.build();
  Vector e(n, 0.0);
  e[ownerCell] = 1.0;
  EXPECT_NEAR(matrix.multiply(e)[ownerCell], 0.0, 1e-12);
  EXPECT_NEAR(rhs[ownerCell], 3.0 * inletVelocity.x, 1e-12);  // -F*phiB = -(-3)*1 = 3
}

TEST(MomentumConvectionTest, ConstantFieldGivesZeroNetContribution) {
  // TODO.md section 29: div(U)=0 for a constant velocity field, so the
  // convective contribution (A*u - rhs) should be exactly zero for a
  // constant u, at every cell.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<MovingWall>(Vector2{2.0, 0.0}));
  }
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{2.0, 0.0});

  // A divergence-free flux field: zero everywhere (matches a stagnant,
  // uniform-boundary-velocity closed domain).
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);
  assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                 builder, rhs);
  const auto matrix = builder.build();
  const Vector uExact(n, 2.0);
  const Vector residual = matrix.multiply(uExact) - rhs;
  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(residual[i], 0.0, 1e-12);
  }
}

TEST(MomentumConvectionTest, MismatchedMassFluxSizeThrows) {
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});
  const SurfaceField massFlux(mesh.numberOfFaces() + 1, 0.0);
  SparseMatrixBuilder builder(n, n);
  Vector rhs(n, 0.0);

  EXPECT_THROW(assembleConvectionContribution(mesh, massFlux, velocity, boundaries,
                                              VelocityComponent::U, builder, rhs),
               InvalidArgumentError);
}

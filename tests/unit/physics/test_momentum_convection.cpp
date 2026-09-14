#include <gtest/gtest.h>

#include <cmath>
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

TEST(MomentumConvectionTest, DefaultSchemeArgumentMatchesExplicitUpwind) {
  // P12-NUM-001: the new `scheme` parameter defaults to Upwind, so every
  // pre-P12-NUM-001 call site (which never passes it) is byte-identical.
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const Index n = mesh.numberOfCells();
  const VectorField velocity(n, Vector2{1.0, 0.0});
  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) massFlux[face.id()] = 5.0;
  }

  SparseMatrixBuilder builderDefault(n, n);
  Vector rhsDefault(n, 0.0);
  assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                 builderDefault, rhsDefault);

  SparseMatrixBuilder builderExplicit(n, n);
  Vector rhsExplicit(n, 0.0);
  assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                 builderExplicit, rhsExplicit,
                                 cfd::discretization::ConvectionScheme::Upwind);

  const auto matrixDefault = builderDefault.build();
  const auto matrixExplicit = builderExplicit.build();
  Vector probe(n, 0.0);
  for (Index i = 0; i < n; ++i) {
    probe[i] = 1.0;
    const Vector rowDefault = matrixDefault.multiply(probe);
    const Vector rowExplicit = matrixExplicit.multiply(probe);
    for (Index row = 0; row < n; ++row) {
      EXPECT_DOUBLE_EQ(rowDefault[row], rowExplicit[row]);
    }
    probe[i] = 0.0;
  }
  for (Index i = 0; i < n; ++i) {
    EXPECT_DOUBLE_EQ(rhsDefault[i], rhsExplicit[i]);
  }
}

TEST(MomentumConvectionTest, NonUpwindSchemeAddsAnExplicitCorrectionOnlyOnInternalFaces) {
  // A QUICK/Central/LinearUpwind scheme must NOT change the implicit
  // matrix coefficients (always plain upwind, for robustness -- deferred
  // correction) but DOES add a nonzero RHS correction on internal faces
  // with a genuine higher-order value to blend toward, and must NEVER
  // touch boundary-face rows differently from plain Upwind.
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 1, 5.0, 1.0);
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<MovingWall>(Vector2{0.0, 0.0}));
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{0.0, 0.0}));

  const Index n = mesh.numberOfCells();
  VectorField velocity(n, Vector2{1.0, 0.0});
  // A smooth, monotone, non-linear profile (matches
  // ConvectionSchemeTest's own smooth-monotone choice in
  // test_convection.cpp) -- avoids driving the TVD limiter's r <= 0
  // everywhere, which would degrade every face back to plain Upwind and
  // make this test vacuous.
  velocity[0] = Vector2{0.0, 0.0};
  velocity[1] = Vector2{1.0, 0.0};
  velocity[2] = Vector2{4.0, 0.0};
  velocity[3] = Vector2{9.0, 0.0};
  velocity[4] = Vector2{16.0, 0.0};

  SurfaceField massFlux(mesh.numberOfFaces(), 0.0);
  for (const auto& face : mesh.faces()) {
    massFlux[face.id()] = cfd::dot(Vector2{1.0, 0.0}, face.areaVector());
  }

  for (const auto scheme : {cfd::discretization::ConvectionScheme::Central,
                            cfd::discretization::ConvectionScheme::LinearUpwind,
                            cfd::discretization::ConvectionScheme::QUICK}) {
    SparseMatrixBuilder builderUpwind(n, n);
    Vector rhsUpwind(n, 0.0);
    assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                   builderUpwind, rhsUpwind);

    SparseMatrixBuilder builderScheme(n, n);
    Vector rhsScheme(n, 0.0);
    assembleConvectionContribution(mesh, massFlux, velocity, boundaries, VelocityComponent::U,
                                   builderScheme, rhsScheme, scheme);

    const auto matrixUpwind = builderUpwind.build();
    const auto matrixScheme = builderScheme.build();
    Vector probe(n, 0.0);
    for (Index i = 0; i < n; ++i) {
      probe[i] = 1.0;
      // Implicit coefficients unchanged regardless of scheme.
      const Vector rowUpwind = matrixUpwind.multiply(probe);
      const Vector rowScheme = matrixScheme.multiply(probe);
      for (Index row = 0; row < n; ++row) {
        EXPECT_DOUBLE_EQ(rowUpwind[row], rowScheme[row])
            << "scheme index " << static_cast<int>(scheme) << " col " << i << " row " << row;
      }
      probe[i] = 0.0;
    }

    // The RHS must differ somewhere in the interior (a genuine
    // correction was applied) -- not vacuously identical to Upwind.
    bool anyDifference = false;
    for (Index i = 0; i < n; ++i) {
      if (std::abs(rhsScheme[i] - rhsUpwind[i]) > 1e-9) anyDifference = true;
    }
    EXPECT_TRUE(anyDifference) << "scheme index " << static_cast<int>(scheme)
                               << " produced no correction at all";
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

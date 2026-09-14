// P12-NUM-003: non-orthogonal correction in the PRODUCTION momentum
// diffusion assembler (cfd::physics::assembleDiffusionContribution, both
// overloads) -- the matrix-assembly counterpart of the
// cfd::discretization::diffusion tests. Verification against exact
// properties, not re-derivations of the implementation:
//   - Cartesian: enabling the correction leaves the assembled system
//     bit-identical (every decomposition is exactly {Sf, 0}).
//   - Distorted: for an exactly LINEAR velocity field with exact
//     Dirichlet (MovingWall) boundary values, the continuous viscous term
//     is zero, so the discrete residual A*u - b must vanish in EVERY row --
//     it does (to round-off) with the correction + least-squares gradient,
//     and does not without the correction (measured, printed).
//   - Conservation: with only non-Dirichlet (Outlet) boundaries, the
//     explicit correction adds exactly zero net RHS over the whole mesh
//     (face-once, equal/opposite).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "DistortedMesh.hpp"
#include "ManufacturedFields.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::algebra::SparseMatrix;
using cfd::algebra::SparseMatrixBuilder;
using cfd::algebra::Vector;
using cfd::boundary::BoundaryConditionSet;
using cfd::discretization::GradientScheme;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleDiffusionContribution;
using cfd::physics::VelocityComponent;

namespace {

Vector2 linearVelocity(const Vector2& p) {
  return Vector2{(2.0 * p.x) + (3.0 * p.y) + 5.0, (-1.0 * p.x) + (4.0 * p.y) - 2.0};
}

Vector2 smoothVelocity(const Vector2& p) {
  return Vector2{cfd::test::phiSmooth(p), 0.5 * cfd::test::phiSmooth(Vector2{p.y, p.x})};
}

// One MovingWall per boundary face carrying the exact velocity at that
// face's centroid (the per-face-patch trick from ManufacturedFields.hpp,
// applied to a vector condition).
template <typename VelocityFunction>
BoundaryConditionSet exactMovingWalls(const Mesh& mesh, VelocityFunction exact) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    const Index faceId = patch.faceIds().front();
    boundaries.set(
        mesh, patch.name(),
        std::make_unique<cfd::boundary::MovingWall>(exact(mesh.face(faceId).centroid())));
  }
  return boundaries;
}

template <typename VelocityFunction>
VectorField sampleVelocity(const Mesh& mesh, VelocityFunction exact) {
  VectorField velocity(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    velocity[cell.id()] = exact(cell.centroid());
  }
  return velocity;
}

struct Assembled {
  SparseMatrix matrix;
  Vector rhs;
};

Assembled assembleConstant(const Mesh& mesh, const VectorField& velocity,
                           const BoundaryConditionSet& boundaries, VelocityComponent component,
                           bool correct, GradientScheme scheme) {
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);
  assembleDiffusionContribution(mesh, 0.37, velocity, boundaries, component, builder, rhs, correct,
                                scheme);
  return Assembled{builder.build(), rhs};
}

Assembled assembleField(const Mesh& mesh, const ScalarField& mu, const VectorField& velocity,
                        const BoundaryConditionSet& boundaries, VelocityComponent component,
                        bool correct, GradientScheme scheme,
                        const VectorField* correctionVelocity = nullptr) {
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);
  assembleDiffusionContribution(mesh, mu, velocity, boundaries, component, builder, rhs, correct,
                                scheme, correctionVelocity);
  return Assembled{builder.build(), rhs};
}

void expectBitIdentical(const Assembled& a, const Assembled& b) {
  ASSERT_EQ(a.matrix.nonZeros(), b.matrix.nonZeros());
  for (Index k = 0; k < a.matrix.nonZeros(); ++k) {
    EXPECT_EQ(a.matrix.valuesData()[k], b.matrix.valuesData()[k]) << "nonzero " << k;
    EXPECT_EQ(a.matrix.columnIndicesData()[k], b.matrix.columnIndicesData()[k]);
  }
  ASSERT_EQ(a.rhs.size(), b.rhs.size());
  for (Index i = 0; i < a.rhs.size(); ++i) {
    EXPECT_EQ(a.rhs[i], b.rhs[i]) << "row " << i;
  }
}

// max_i |(A u - b)_i| for one velocity component.
Real maxResidual(const Assembled& system, const VectorField& velocity,
                 VelocityComponent component) {
  Vector x(velocity.size());
  for (Index i = 0; i < velocity.size(); ++i) {
    x[i] = (component == VelocityComponent::U) ? velocity[i].x : velocity[i].y;
  }
  const Vector ax = system.matrix.multiply(x);
  Real worst = 0.0;
  for (Index i = 0; i < x.size(); ++i) {
    worst = std::max(worst, std::abs(ax[i] - system.rhs[i]));
  }
  return worst;
}

}  // namespace

TEST(MomentumNonOrthogonalTest, CartesianCorrectionIsBitIdenticalForBothOverloads) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(7, 5, 1.0, 0.8);
  const auto boundaries = exactMovingWalls(mesh, smoothVelocity);
  const VectorField velocity = sampleVelocity(mesh, smoothVelocity);
  ScalarField mu(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    mu[cell.id()] = 0.2 + (0.1 * cell.centroid().x);
  }

  for (const VelocityComponent component : {VelocityComponent::U, VelocityComponent::V}) {
    for (const GradientScheme scheme : {GradientScheme::GreenGauss, GradientScheme::LeastSquares}) {
      expectBitIdentical(assembleConstant(mesh, velocity, boundaries, component, false, scheme),
                         assembleConstant(mesh, velocity, boundaries, component, true, scheme));
      expectBitIdentical(assembleField(mesh, mu, velocity, boundaries, component, false, scheme),
                         assembleField(mesh, mu, velocity, boundaries, component, true, scheme));
    }
  }
}

TEST(MomentumNonOrthogonalTest, LinearVelocityResidualVanishesInEveryRowWhenCorrected) {
  const Mesh mesh = cfd::test::perFaceBoundaryMesh(
      cfd::test::createDistortedQuad2D(10, 10, 1.0, 1.0, 0.3 / 10.0));
  const auto boundaries = exactMovingWalls(mesh, linearVelocity);
  const VectorField velocity = sampleVelocity(mesh, linearVelocity);
  const ScalarField mu(mesh.numberOfCells(), 0.37);

  for (const VelocityComponent component : {VelocityComponent::U, VelocityComponent::V}) {
    const Real uncorrected = maxResidual(assembleField(mesh, mu, velocity, boundaries, component,
                                                       false, GradientScheme::LeastSquares),
                                         velocity, component);
    const Real correctedLs = maxResidual(assembleField(mesh, mu, velocity, boundaries, component,
                                                       true, GradientScheme::LeastSquares),
                                         velocity, component);
    const Real correctedGg = maxResidual(
        assembleField(mesh, mu, velocity, boundaries, component, true, GradientScheme::GreenGauss),
        velocity, component);
    const Real correctedConstLs = maxResidual(
        assembleConstant(mesh, velocity, boundaries, component, true, GradientScheme::LeastSquares),
        velocity, component);
    std::printf(
        "\nMomentum diffusion, linear velocity, distorted 10x10 (0.3h), component %s: "
        "max|Au-b| uncorrected %.4g, corrected(GG) %.4g, corrected(LS) %.3g\n",
        (component == VelocityComponent::U) ? "U" : "V", uncorrected, correctedGg, correctedLs);
    EXPECT_LT(correctedLs, 1e-12);
    EXPECT_LT(correctedConstLs, 1e-12);
    EXPECT_GT(uncorrected, 1e-4);
    // Skewness-corrected Green-Gauss (P12-NUM-003 continuation): 1.3e-4
    // with the plain Green-Gauss gradient, 3e-13 now.
    EXPECT_LT(correctedGg, 1e-10);
  }
}

TEST(MomentumNonOrthogonalTest, ExplicitCorrectionIsGloballyConservativeWithoutDirichletFaces) {
  // Outlet everywhere: no boundary face is Dirichlet-type, so no boundary
  // correction; every internal correction is added to one row and
  // subtracted from another -- the RHS change must sum to zero.
  const Mesh mesh = cfd::test::createDistortedQuad2D(9, 9, 1.0, 1.0, 0.4 / 9.0);
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::Outlet>());
  }
  const VectorField velocity = sampleVelocity(mesh, smoothVelocity);
  const ScalarField mu(mesh.numberOfCells(), 0.37);

  for (const VelocityComponent component : {VelocityComponent::U, VelocityComponent::V}) {
    const auto uncorrected = assembleField(mesh, mu, velocity, boundaries, component, false,
                                           GradientScheme::LeastSquares);
    const auto corrected = assembleField(mesh, mu, velocity, boundaries, component, true,
                                         GradientScheme::LeastSquares);
    Real netChange = 0.0;
    Real maxChange = 0.0;
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      netChange += corrected.rhs[i] - uncorrected.rhs[i];
      maxChange = std::max(maxChange, std::abs(corrected.rhs[i] - uncorrected.rhs[i]));
    }
    EXPECT_GT(maxChange, 1e-6) << "the correction should actually be active";
    EXPECT_NEAR(netChange, 0.0, 1e-14);
  }
}

TEST(MomentumNonOrthogonalTest, CorrectionVelocityDefaultsToVelocityAndIsValidated) {
  const Mesh mesh =
      cfd::test::perFaceBoundaryMesh(cfd::test::createDistortedQuad2D(6, 6, 1.0, 1.0, 0.3 / 6.0));
  const auto boundaries = exactMovingWalls(mesh, smoothVelocity);
  const VectorField velocity = sampleVelocity(mesh, smoothVelocity);
  const ScalarField mu(mesh.numberOfCells(), 0.37);

  // Passing `velocity` itself as the correction field is the same as
  // passing nothing.
  expectBitIdentical(assembleField(mesh, mu, velocity, boundaries, VelocityComponent::U, true,
                                   GradientScheme::LeastSquares),
                     assembleField(mesh, mu, velocity, boundaries, VelocityComponent::U, true,
                                   GradientScheme::LeastSquares, &velocity));

  // A different correction field changes only the RHS, never the matrix.
  const VectorField other = sampleVelocity(mesh, linearVelocity);
  const auto a = assembleField(mesh, mu, velocity, boundaries, VelocityComponent::U, true,
                               GradientScheme::LeastSquares);
  const auto b = assembleField(mesh, mu, velocity, boundaries, VelocityComponent::U, true,
                               GradientScheme::LeastSquares, &other);
  ASSERT_EQ(a.matrix.nonZeros(), b.matrix.nonZeros());
  for (Index k = 0; k < a.matrix.nonZeros(); ++k) {
    EXPECT_EQ(a.matrix.valuesData()[k], b.matrix.valuesData()[k]);
  }
  Real maxRhsDifference = 0.0;
  for (Index i = 0; i < a.rhs.size(); ++i) {
    maxRhsDifference = std::max(maxRhsDifference, std::abs(a.rhs[i] - b.rhs[i]));
  }
  EXPECT_GT(maxRhsDifference, 1e-6);

  const VectorField wrongSize(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  SparseMatrixBuilder builder(mesh.numberOfCells(), mesh.numberOfCells());
  Vector rhs(mesh.numberOfCells(), 0.0);
  EXPECT_THROW(
      assembleDiffusionContribution(mesh, mu, velocity, boundaries, VelocityComponent::U, builder,
                                    rhs, true, GradientScheme::LeastSquares, &wrongSize),
      cfd::InvalidArgumentError);
}

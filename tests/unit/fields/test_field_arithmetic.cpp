#include <gtest/gtest.h>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;

// --- Size-mismatch rejection ------------------------------------------

TEST(FieldArithmetic, ScalarFieldRejectsSizeMismatch) {
  const ScalarField a(3, 1.0);
  const ScalarField b(4, 2.0);

  EXPECT_THROW((void)(a + b), cfd::InvalidArgumentError);
  EXPECT_THROW((void)(a - b), cfd::InvalidArgumentError);

  ScalarField c = a;
  EXPECT_THROW(c += b, cfd::InvalidArgumentError);
  EXPECT_THROW(c -= b, cfd::InvalidArgumentError);
}

TEST(FieldArithmetic, ScalarFieldMismatchLeavesOperandUnchanged) {
  const ScalarField a(3, 1.0);
  const ScalarField b(4, 2.0);

  ScalarField c = a;
  EXPECT_THROW(c += b, cfd::InvalidArgumentError);
  // c must be untouched: size is validated before any element is mutated.
  for (ScalarField::size_type i = 0; i < c.size(); ++i) {
    EXPECT_DOUBLE_EQ(c[i], 1.0);
  }
}

TEST(FieldArithmetic, VectorFieldRejectsSizeMismatch) {
  const VectorField a(3, Vector2{1.0, 1.0});
  const VectorField b(4, Vector2{2.0, 2.0});

  EXPECT_THROW((void)(a + b), cfd::InvalidArgumentError);
  EXPECT_THROW((void)(a - b), cfd::InvalidArgumentError);

  VectorField c = a;
  EXPECT_THROW(c += b, cfd::InvalidArgumentError);
  EXPECT_THROW(c -= b, cfd::InvalidArgumentError);
}

// --- Arithmetic identities ----------------------------------------------

TEST(FieldArithmetic, ScalarIdentities) {
  const ScalarField a(4, 3.0);
  const ScalarField zero(4, 0.0);

  const ScalarField sum = a + zero;
  for (ScalarField::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(sum[i], a[i]);
  }

  const ScalarField diff = a - a;
  for (ScalarField::size_type i = 0; i < diff.size(); ++i) {
    EXPECT_DOUBLE_EQ(diff[i], 0.0);
  }

  const ScalarField scaledByOne = a * 1.0;
  for (ScalarField::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(scaledByOne[i], a[i]);
  }

  const ScalarField dividedByOne = a / 1.0;
  for (ScalarField::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(dividedByOne[i], a[i]);
  }
}

TEST(FieldArithmetic, VectorIdentities) {
  const VectorField u(4, Vector2{1.5, -2.5});

  const VectorField selfDiff = u - u;
  for (VectorField::size_type i = 0; i < selfDiff.size(); ++i) {
    EXPECT_DOUBLE_EQ(selfDiff[i].x, 0.0);
    EXPECT_DOUBLE_EQ(selfDiff[i].y, 0.0);
  }

  const VectorField scaledByOne = u * 1.0;
  for (VectorField::size_type i = 0; i < u.size(); ++i) {
    EXPECT_DOUBLE_EQ(scaledByOne[i].x, u[i].x);
    EXPECT_DOUBLE_EQ(scaledByOne[i].y, u[i].y);
  }
}

// --- Empty fields ----------------------------------------------------------

TEST(FieldArithmetic, EmptyFieldsAreAddable) {
  const ScalarField a;
  const ScalarField b;
  const ScalarField c = a + b;
  EXPECT_EQ(c.size(), 0U);
  EXPECT_TRUE(c.empty());
}

// --- Mesh/field size integration checkpoints ------------------------------

TEST(FieldArithmetic, TwoByTwoMeshIntegration) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);

  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  EXPECT_EQ(pressure.size(), 4U);
  EXPECT_EQ(velocity.size(), 4U);
  EXPECT_EQ(massFlux.size(), 12U);
}

TEST(FieldArithmetic, TwentyByTwentyMeshIntegration) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0);

  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const SurfaceField massFlux(mesh.numberOfFaces(), 0.0);

  EXPECT_EQ(pressure.size(), 400U);
  EXPECT_EQ(velocity.size(), 400U);
  EXPECT_EQ(massFlux.size(), 840U);
}

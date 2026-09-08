#include <gtest/gtest.h>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/VectorField.hpp"

using cfd::Vector2;
using cfd::fields::VectorField;

TEST(VectorFieldTest, ConstructionAndInitialization) {
  const VectorField field(2, Vector2{1.0, 2.0});
  EXPECT_EQ(field.size(), 2U);
  EXPECT_DOUBLE_EQ(field[0].x, 1.0);
  EXPECT_DOUBLE_EQ(field[0].y, 2.0);
}

TEST(VectorFieldTest, ElementwiseAdditionExactValues) {
  VectorField u(2);
  VectorField v(2);
  u[0] = Vector2{1.0, 2.0};
  u[1] = Vector2{3.0, 4.0};
  v[0] = Vector2{5.0, 6.0};
  v[1] = Vector2{7.0, 8.0};

  const VectorField w = u + v;
  EXPECT_DOUBLE_EQ(w[0].x, 6.0);
  EXPECT_DOUBLE_EQ(w[0].y, 8.0);
  EXPECT_DOUBLE_EQ(w[1].x, 10.0);
  EXPECT_DOUBLE_EQ(w[1].y, 12.0);
}

TEST(VectorFieldTest, ElementwiseSubtraction) {
  const VectorField u(2, Vector2{5.0, 5.0});
  const VectorField v(2, Vector2{2.0, 1.0});
  const VectorField w = u - v;
  for (VectorField::size_type i = 0; i < w.size(); ++i) {
    EXPECT_DOUBLE_EQ(w[i].x, 3.0);
    EXPECT_DOUBLE_EQ(w[i].y, 4.0);
  }
}

TEST(VectorFieldTest, ScalarMultiplicationBothOrders) {
  const VectorField u(2, Vector2{1.0, 2.0});
  const VectorField a = u * 2.0;
  const VectorField b = 2.0 * u;
  for (VectorField::size_type i = 0; i < u.size(); ++i) {
    EXPECT_DOUBLE_EQ(a[i].x, 2.0);
    EXPECT_DOUBLE_EQ(a[i].y, 4.0);
    EXPECT_DOUBLE_EQ(b[i].x, 2.0);
    EXPECT_DOUBLE_EQ(b[i].y, 4.0);
  }
}

TEST(VectorFieldTest, ScalarDivision) {
  const VectorField u(2, Vector2{4.0, 8.0});
  const VectorField v = u / 2.0;
  for (VectorField::size_type i = 0; i < v.size(); ++i) {
    EXPECT_DOUBLE_EQ(v[i].x, 2.0);
    EXPECT_DOUBLE_EQ(v[i].y, 4.0);
  }
}

TEST(VectorFieldTest, DivisionByZeroThrows) {
  const VectorField u(2, Vector2{1.0, 1.0});
  EXPECT_THROW((void)(u / 0.0), cfd::InvalidArgumentError);
}

TEST(VectorFieldTest, SelfSubtractionIsZeroField) {
  const VectorField u(3, Vector2{2.0, -3.0});
  const VectorField zero = u - u;
  for (VectorField::size_type i = 0; i < zero.size(); ++i) {
    EXPECT_DOUBLE_EQ(zero[i].x, 0.0);
    EXPECT_DOUBLE_EQ(zero[i].y, 0.0);
  }
}

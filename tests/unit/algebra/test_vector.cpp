#include <gtest/gtest.h>

#include <limits>

#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Exception.hpp"

using cfd::algebra::Vector;

TEST(AlgebraVector, ConstructionAndInitialization) {
  const Vector a{1.0, 2.0, 3.0};
  EXPECT_EQ(a.size(), 3U);
  EXPECT_DOUBLE_EQ(a[0], 1.0);
  EXPECT_DOUBLE_EQ(a[2], 3.0);

  const Vector b(4, 2.5);
  EXPECT_EQ(b.size(), 4U);
  for (Vector::size_type i = 0; i < b.size(); ++i) {
    EXPECT_DOUBLE_EQ(b[i], 2.5);
  }
}

TEST(AlgebraVector, Addition) {
  const Vector a{1.0, 2.0, 3.0};
  const Vector b{4.0, 5.0, 6.0};
  const Vector c = a + b;
  EXPECT_DOUBLE_EQ(c[0], 5.0);
  EXPECT_DOUBLE_EQ(c[1], 7.0);
  EXPECT_DOUBLE_EQ(c[2], 9.0);
}

TEST(AlgebraVector, Subtraction) {
  const Vector a{4.0, 5.0, 6.0};
  const Vector b{1.0, 2.0, 3.0};
  const Vector c = a - b;
  EXPECT_DOUBLE_EQ(c[0], 3.0);
  EXPECT_DOUBLE_EQ(c[1], 3.0);
  EXPECT_DOUBLE_EQ(c[2], 3.0);
}

TEST(AlgebraVector, ScalarMultiplicationBothOrders) {
  const Vector a{1.0, 2.0, 3.0};
  const Vector b = a * 2.0;
  const Vector c = 2.0 * a;
  for (Vector::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(b[i], a[i] * 2.0);
    EXPECT_DOUBLE_EQ(c[i], a[i] * 2.0);
  }
}

TEST(AlgebraVector, ScalarDivision) {
  const Vector a{2.0, 4.0, 6.0};
  const Vector b = a / 2.0;
  EXPECT_DOUBLE_EQ(b[0], 1.0);
  EXPECT_DOUBLE_EQ(b[1], 2.0);
  EXPECT_DOUBLE_EQ(b[2], 3.0);
}

TEST(AlgebraVector, DivisionByZeroThrows) {
  const Vector a{1.0, 2.0};
  EXPECT_THROW((void)(a / 0.0), cfd::InvalidArgumentError);
}

TEST(AlgebraVector, DotProduct) {
  const Vector a{1.0, 2.0, 3.0};
  const Vector b{4.0, 5.0, 6.0};
  EXPECT_DOUBLE_EQ(cfd::algebra::dot(a, b), 32.0);
}

TEST(AlgebraVector, Norms) {
  const Vector v{3.0, -4.0};
  EXPECT_DOUBLE_EQ(cfd::algebra::l1Norm(v), 7.0);
  EXPECT_DOUBLE_EQ(cfd::algebra::l2Norm(v), 5.0);
  EXPECT_DOUBLE_EQ(cfd::algebra::infinityNorm(v), 4.0);
}

TEST(AlgebraVector, SizeMismatchRejected) {
  const Vector a{1.0, 2.0, 3.0};
  const Vector b{1.0, 2.0, 3.0, 4.0};

  EXPECT_THROW((void)(a + b), cfd::InvalidArgumentError);
  EXPECT_THROW((void)(a - b), cfd::InvalidArgumentError);
  EXPECT_THROW((void)cfd::algebra::dot(a, b), cfd::InvalidArgumentError);
}

TEST(AlgebraVector, AllFiniteDetection) {
  const Vector finite{1.0, 2.0, 3.0};
  EXPECT_TRUE(finite.allFinite());

  Vector withNan{1.0, 2.0, 3.0};
  withNan[1] = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_FALSE(withNan.allFinite());

  Vector withInf{1.0, 2.0, 3.0};
  withInf[2] = std::numeric_limits<cfd::Real>::infinity();
  EXPECT_FALSE(withInf.allFinite());
}

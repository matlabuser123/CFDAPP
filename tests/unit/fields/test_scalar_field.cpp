#include <gtest/gtest.h>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"

using cfd::fields::ScalarField;

TEST(ScalarFieldTest, ConstructionAndInitialization) {
  const ScalarField field(3, 5.0);
  EXPECT_EQ(field.size(), 3U);
  EXPECT_DOUBLE_EQ(field[0], 5.0);
}

TEST(ScalarFieldTest, MutationIsIndependentAfterCopy) {
  const ScalarField a(3, 1.0);
  ScalarField b = a;
  b[0] = 2.0;
  EXPECT_DOUBLE_EQ(a[0], 1.0);
  EXPECT_DOUBLE_EQ(b[0], 2.0);
}

TEST(ScalarFieldTest, Fill) {
  ScalarField field(4, 0.0);
  field.fill(3.0);
  for (ScalarField::size_type i = 0; i < field.size(); ++i) {
    EXPECT_DOUBLE_EQ(field[i], 3.0);
  }
}

TEST(ScalarFieldTest, ElementwiseAddition) {
  const ScalarField a(3, 1.0);
  const ScalarField b(3, 2.0);
  const ScalarField c = a + b;
  for (ScalarField::size_type i = 0; i < c.size(); ++i) {
    EXPECT_DOUBLE_EQ(c[i], 3.0);
  }
}

TEST(ScalarFieldTest, ElementwiseSubtraction) {
  const ScalarField a(3, 5.0);
  const ScalarField b(3, 2.0);
  const ScalarField c = a - b;
  for (ScalarField::size_type i = 0; i < c.size(); ++i) {
    EXPECT_DOUBLE_EQ(c[i], 3.0);
  }
}

TEST(ScalarFieldTest, ScalarMultiplicationBothOrders) {
  const ScalarField a(3, 2.0);
  const ScalarField b = a * 2.0;
  const ScalarField c = 2.0 * a;
  for (ScalarField::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(b[i], 4.0);
    EXPECT_DOUBLE_EQ(c[i], 4.0);
  }
}

TEST(ScalarFieldTest, ScalarDivision) {
  const ScalarField a(3, 6.0);
  const ScalarField b = a / 2.0;
  for (ScalarField::size_type i = 0; i < b.size(); ++i) {
    EXPECT_DOUBLE_EQ(b[i], 3.0);
  }
}

TEST(ScalarFieldTest, DivisionByZeroThrows) {
  const ScalarField a(3, 1.0);
  EXPECT_THROW((void)(a / 0.0), cfd::InvalidArgumentError);
}

TEST(ScalarFieldTest, CompoundOperators) {
  ScalarField a(3, 1.0);
  const ScalarField b(3, 2.0);

  a += b;
  for (ScalarField::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(a[i], 3.0);
  }

  a -= b;
  for (ScalarField::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(a[i], 1.0);
  }

  a *= 4.0;
  for (ScalarField::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(a[i], 4.0);
  }

  a /= 2.0;
  for (ScalarField::size_type i = 0; i < a.size(); ++i) {
    EXPECT_DOUBLE_EQ(a[i], 2.0);
  }
}

TEST(ScalarFieldTest, ExplicitValuesFromSpecExample) {
  ScalarField a(3);
  ScalarField b(3);
  a[0] = 1.0;
  a[1] = 2.0;
  a[2] = 3.0;
  b[0] = 4.0;
  b[1] = 5.0;
  b[2] = 6.0;

  const ScalarField sum = a + b;
  EXPECT_DOUBLE_EQ(sum[0], 5.0);
  EXPECT_DOUBLE_EQ(sum[1], 7.0);
  EXPECT_DOUBLE_EQ(sum[2], 9.0);

  const ScalarField diff = a - b;
  EXPECT_DOUBLE_EQ(diff[0], -3.0);
  EXPECT_DOUBLE_EQ(diff[1], -3.0);
  EXPECT_DOUBLE_EQ(diff[2], -3.0);

  const ScalarField scaled = a * 2.0;
  EXPECT_DOUBLE_EQ(scaled[0], 2.0);
  EXPECT_DOUBLE_EQ(scaled[1], 4.0);
  EXPECT_DOUBLE_EQ(scaled[2], 6.0);
}

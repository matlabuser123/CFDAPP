#include <gtest/gtest.h>

#include <stdexcept>

#include "cfd/core/Types.hpp"
#include "cfd/fields/Field.hpp"

using cfd::Real;
using cfd::fields::Field;

TEST(Field, DefaultConstructionIsEmpty) {
  const Field<Real> field;
  EXPECT_EQ(field.size(), 0U);
  EXPECT_TRUE(field.empty());
}

TEST(Field, SizedConstructionValueInitializes) {
  const Field<Real> field(4);
  EXPECT_EQ(field.size(), 4U);
  for (Field<Real>::size_type i = 0; i < field.size(); ++i) {
    EXPECT_DOUBLE_EQ(field[i], 0.0);
  }
}

TEST(Field, InitializedConstructionFillsValue) {
  const Field<Real> field(4, 2.5);
  EXPECT_EQ(field.size(), 4U);
  EXPECT_DOUBLE_EQ(field[0], 2.5);
  EXPECT_DOUBLE_EQ(field[3], 2.5);
}

TEST(Field, IndexingReadsAndWrites) {
  Field<Real> field(3, 0.0);
  field[1] = 9.0;
  EXPECT_DOUBLE_EQ(field[1], 9.0);
}

TEST(Field, CheckedAccessWorksInRange) {
  Field<Real> field(3, 1.0);
  EXPECT_NO_THROW((void)field.at(0));
  EXPECT_NO_THROW((void)field.at(1));
  EXPECT_NO_THROW((void)field.at(2));
}

TEST(Field, CheckedAccessThrowsOutOfRange) {
  Field<Real> field(3);
  EXPECT_THROW((void)field.at(3), std::out_of_range);
}

TEST(Field, IterationVisitsEveryElement) {
  const Field<Real> field(3, 1.0);
  Real total = 0.0;
  for (const Real& value : field) {
    total += value;
  }
  EXPECT_DOUBLE_EQ(total, 3.0);
}

TEST(Field, DataProvidesContiguousAccess) {
  const Field<Real> field(3, 2.0);
  const Real* raw = field.data();
  EXPECT_DOUBLE_EQ(raw[0], 2.0);
  EXPECT_DOUBLE_EQ(raw[1], 2.0);
  EXPECT_DOUBLE_EQ(raw[2], 2.0);
}

TEST(Field, FillOverwritesAllElements) {
  Field<Real> field(3, 1.0);
  field.fill(7.0);
  for (const Real& value : field) {
    EXPECT_DOUBLE_EQ(value, 7.0);
  }
}

TEST(Field, CopyIsIndependent) {
  const Field<Real> a(3, 1.0);
  Field<Real> b = a;
  b[0] = 99.0;
  EXPECT_DOUBLE_EQ(a[0], 1.0);
  EXPECT_DOUBLE_EQ(b[0], 99.0);
}

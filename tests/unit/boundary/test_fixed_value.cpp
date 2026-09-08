#include <gtest/gtest.h>

#include <limits>

#include "cfd/boundary/FixedValue.hpp"
#include "cfd/core/Exception.hpp"

using cfd::boundary::BoundaryConditionType;
using cfd::boundary::FixedValue;

TEST(FixedValueTest, BoundaryValueIgnoresOwnerValue) {
  const FixedValue bc(5.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(0.0, 1.0), 5.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(100.0, 1.0), 5.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(-10.0, 1.0), 5.0);
}

TEST(FixedValueTest, ExposesTypeAndName) {
  const FixedValue bc(1.0);
  EXPECT_EQ(bc.type(), BoundaryConditionType::FixedValue);
  EXPECT_EQ(bc.name(), "FixedValue");
}

TEST(FixedValueTest, RejectsNonFiniteValue) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  const cfd::Real inf = std::numeric_limits<cfd::Real>::infinity();
  EXPECT_THROW((FixedValue(nan)), cfd::InvalidArgumentError);
  EXPECT_THROW((FixedValue(inf)), cfd::InvalidArgumentError);
}

#include <gtest/gtest.h>

#include <limits>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/core/Exception.hpp"

using cfd::boundary::FixedGradient;

TEST(FixedGradientTest, NonzeroGradient) {
  const FixedGradient bc(2.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(10.0, 0.5), 11.0);
}

TEST(FixedGradientTest, ZeroGradientReturnsOwnerValue) {
  const FixedGradient bc(0.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(7.0, 0.5), 7.0);
  EXPECT_DOUBLE_EQ(bc.boundaryValue(-3.0, 2.0), -3.0);
}

TEST(FixedGradientTest, RejectsNonFiniteGradient) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((FixedGradient(nan)), cfd::InvalidArgumentError);
}

TEST(FixedGradientTest, RejectsInvalidDistance) {
  const FixedGradient bc(1.0);
  EXPECT_THROW((void)bc.boundaryValue(1.0, 0.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)bc.boundaryValue(1.0, -1.0), cfd::InvalidArgumentError);

  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((void)bc.boundaryValue(1.0, nan), cfd::InvalidArgumentError);
}

#include <gtest/gtest.h>

#include <limits>

#include "cfd/boundary/Inlet.hpp"
#include "cfd/core/Exception.hpp"

using cfd::Vector2;
using cfd::boundary::Inlet;

TEST(InletTest, ReturnsPrescribedVelocityExactly) {
  const Inlet inlet(Vector2{1.0, 0.0});
  const Vector2 result = inlet.boundaryValue(Vector2{0.0, 0.0}, 1.0, Vector2{-1.0, 0.0});
  EXPECT_DOUBLE_EQ(result.x, 1.0);
  EXPECT_DOUBLE_EQ(result.y, 0.0);
}

TEST(InletTest, InflowDotProductWithOutwardNormalIsNegative) {
  // Left boundary: outward normal (-1,0); inlet flowing in the +x
  // direction indicates flow into the domain.
  const Inlet inlet(Vector2{1.0, 0.0});
  const Vector2 leftOutwardNormal{-1.0, 0.0};
  EXPECT_LT(cfd::dot(inlet.velocity(), leftOutwardNormal), 0.0);
}

TEST(InletTest, RejectsNonFiniteVelocity) {
  const cfd::Real nan = std::numeric_limits<cfd::Real>::quiet_NaN();
  EXPECT_THROW((Inlet(Vector2{1.0, nan})), cfd::InvalidArgumentError);
}

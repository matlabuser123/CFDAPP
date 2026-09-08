#include <gtest/gtest.h>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Outlet.hpp"

using cfd::Vector2;
using cfd::boundary::FixedGradient;
using cfd::boundary::Outlet;

TEST(OutletTest, VelocityZeroGradientReturnsOwnerValue) {
  const Outlet outlet;
  const Vector2 uOwner{2.0, 0.5};
  const Vector2 uBoundary = outlet.boundaryValue(uOwner, 1.0, Vector2{1.0, 0.0});
  EXPECT_DOUBLE_EQ(uBoundary.x, uOwner.x);
  EXPECT_DOUBLE_EQ(uBoundary.y, uOwner.y);
}

TEST(OutletTest, ScalarZeroGradientOutletIsFixedGradientZero) {
  // A scalar (e.g. pressure) zero-gradient outlet is exactly
  // FixedGradient(0.0) -- see Outlet.hpp.
  const FixedGradient scalarOutlet(0.0);
  EXPECT_DOUBLE_EQ(scalarOutlet.boundaryValue(7.0, 0.5), 7.0);
}

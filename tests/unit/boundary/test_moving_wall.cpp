#include <gtest/gtest.h>

#include <limits>

#include "cfd/boundary/MovingWall.hpp"
#include "cfd/core/Exception.hpp"

using cfd::Vector2;
using cfd::boundary::MovingWall;

TEST(MovingWallTest, CavityTopWallIsImpermeableAndTangential) {
  const MovingWall wall(Vector2{1.0, 0.0});
  const Vector2 n{0.0, 1.0};
  const Vector2 uWall = wall.boundaryValue(Vector2{0.0, 0.0}, 1.0, n);

  EXPECT_DOUBLE_EQ(cfd::dot(uWall, n), 0.0);  // impermeable: no normal component
  EXPECT_DOUBLE_EQ(cfd::magnitude(uWall), 1.0);
}

TEST(MovingWallTest, ReturnsConfiguredVelocityRegardlessOfOwner) {
  const MovingWall wall(Vector2{2.0, -1.0});
  const Vector2 result = wall.boundaryValue(Vector2{99.0, 99.0}, 1.0, Vector2{0.0, 1.0});
  EXPECT_DOUBLE_EQ(result.x, 2.0);
  EXPECT_DOUBLE_EQ(result.y, -1.0);
}

TEST(MovingWallTest, RejectsNonFiniteVelocity) {
  const cfd::Real inf = std::numeric_limits<cfd::Real>::infinity();
  EXPECT_THROW((MovingWall(Vector2{inf, 0.0})), cfd::InvalidArgumentError);
}

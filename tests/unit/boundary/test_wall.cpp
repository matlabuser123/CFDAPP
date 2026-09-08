#include <gtest/gtest.h>

#include "cfd/boundary/Wall.hpp"

using cfd::Vector2;
using cfd::boundary::Wall;

TEST(WallTest, VelocityIsZeroRegardlessOfOwnerValue) {
  const Wall wall;
  const Vector2 result = wall.boundaryValue(Vector2{5.0, -3.0}, 1.0, Vector2{1.0, 0.0});
  EXPECT_DOUBLE_EQ(result.x, 0.0);
  EXPECT_DOUBLE_EQ(result.y, 0.0);
}

TEST(WallTest, NormalAndTangentialComponentsAreZeroOnAllOrientations) {
  const Wall wall;
  const Vector2 normals[4] = {{-1.0, 0.0}, {1.0, 0.0}, {0.0, -1.0}, {0.0, 1.0}};
  for (const Vector2& n : normals) {
    const Vector2 uWall = wall.boundaryValue(Vector2{1.0, 1.0}, 1.0, n);
    EXPECT_DOUBLE_EQ(cfd::dot(uWall, n), 0.0);
    EXPECT_DOUBLE_EQ(cfd::magnitude(uWall), 0.0);
  }
}

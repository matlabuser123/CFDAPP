#include <gtest/gtest.h>

#include "cfd/boundary/Symmetry.hpp"
#include "cfd/core/Exception.hpp"

using cfd::Vector2;
using cfd::boundary::Symmetry;

TEST(SymmetryTest, RightPlaneRemovesNormalComponent) {
  const Symmetry symmetry;
  const Vector2 result = symmetry.boundaryValue(Vector2{3.0, 4.0}, 1.0, Vector2{1.0, 0.0});
  EXPECT_DOUBLE_EQ(result.x, 0.0);
  EXPECT_DOUBLE_EQ(result.y, 4.0);
}

TEST(SymmetryTest, TopPlaneRemovesNormalComponent) {
  const Symmetry symmetry;
  const Vector2 result = symmetry.boundaryValue(Vector2{3.0, 4.0}, 1.0, Vector2{0.0, 1.0});
  EXPECT_DOUBLE_EQ(result.x, 3.0);
  EXPECT_DOUBLE_EQ(result.y, 0.0);
}

TEST(SymmetryTest, ResultIsAlwaysNormalToPlane) {
  const Symmetry symmetry;
  const Vector2 normals[4] = {{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}};
  for (const Vector2& n : normals) {
    const Vector2 result = symmetry.boundaryValue(Vector2{3.0, 4.0}, 1.0, n);
    EXPECT_NEAR(cfd::dot(result, n), 0.0, 1e-12);
  }
}

TEST(SymmetryTest, RejectsNonUnitNormal) {
  const Symmetry symmetry;
  EXPECT_THROW((void)symmetry.boundaryValue(Vector2{1.0, 1.0}, 1.0, Vector2{2.0, 0.0}),
               cfd::InvalidArgumentError);
}

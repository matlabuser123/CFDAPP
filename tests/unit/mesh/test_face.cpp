#include <gtest/gtest.h>

#include <optional>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/Face.hpp"

TEST(MeshFace, InternalFaceStoresOwnerAndNeighbor) {
  const cfd::mesh::Face face(2, 0, 1, cfd::Vector2{1.0, 0.5}, cfd::Vector2{1.0, 0.0});

  EXPECT_EQ(face.id(), 2U);
  EXPECT_EQ(face.owner(), 0U);
  ASSERT_TRUE(face.neighbor().has_value());
  EXPECT_EQ(*face.neighbor(), 1U);
  EXPECT_FALSE(face.isBoundary());
}

TEST(MeshFace, BoundaryFaceHasNoNeighbor) {
  const cfd::mesh::Face face(0, 0, std::nullopt, cfd::Vector2{0.0, 0.5}, cfd::Vector2{-1.0, 0.0});

  EXPECT_TRUE(face.isBoundary());
  EXPECT_FALSE(face.neighbor().has_value());
}

TEST(MeshFace, AreaIsMagnitudeOfAreaVector) {
  const cfd::mesh::Face face(0, 0, std::nullopt, cfd::Vector2{0.0, 0.0}, cfd::Vector2{3.0, 4.0});
  EXPECT_DOUBLE_EQ(face.area(), 5.0);
}

TEST(MeshFace, RejectsZeroAreaVector) {
  EXPECT_THROW(
      cfd::mesh::Face(0, 0, std::nullopt, (cfd::Vector2{0.0, 0.0}), (cfd::Vector2{0.0, 0.0})),
      cfd::InvalidArgumentError);
}

TEST(MeshFace, RejectsOwnerEqualToNeighbor) {
  EXPECT_THROW(cfd::mesh::Face(0, 2, 2, (cfd::Vector2{0.0, 0.0}), (cfd::Vector2{1.0, 0.0})),
               cfd::InvalidArgumentError);
}

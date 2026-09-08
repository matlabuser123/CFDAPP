#include <gtest/gtest.h>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"

TEST(MeshBoundaryPatch, StoresNameAndFaceIds) {
  const cfd::mesh::BoundaryPatch patch("top", {1, 2, 3});

  EXPECT_EQ(patch.name(), "top");
  ASSERT_EQ(patch.faceIds().size(), 3U);
  EXPECT_EQ(patch.size(), 3U);
  EXPECT_EQ(patch.faceIds()[0], 1U);
}

TEST(MeshBoundaryPatch, AllowsEmptyFaceList) {
  const cfd::mesh::BoundaryPatch patch("empty", {});
  EXPECT_EQ(patch.size(), 0U);
}

TEST(MeshBoundaryPatch, RejectsEmptyName) {
  EXPECT_THROW(cfd::mesh::BoundaryPatch("", {1}), cfd::InvalidArgumentError);
}
